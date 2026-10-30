# 설계→코딩 인터페이스: pulse_engine (펄스 구간 공급·위치 계수)

| 모듈 ID | 버전 | 작성 | 대상 언어/플랫폼 | 추적 SRS ID |
|---|---|---|---|---|
| MOD-07 | 0.1 | architect | C(MCU 무관, MOD-02 위) | FR-001, FR-003, FR-006, FR-007, FR-009, FR-010, NFR-001, NFR-002, NFR-003, NFR-004 |

## 1. 책임 (이 모듈이 하는 일 / 하지 않는 일)
- 한다: 축별로 운동 계획기(MOD-08)가 내는 "다음 구간(펄스 수, 주파수)"을 하드웨어 구간(주기 틱, 펄스 수)으로 바꿔 `hal_pulse`에 공급한다. 구간 완료 이벤트(L1 ISR)에서 다음 구간을 계산·투입한다. 지령 위치(= 기준 위치 + 방향 × 하드웨어 계수 누계)를 관리하고 원자적으로 제공한다. 즉시 정지·감속 정지 요청을 처리한다. 언더런·계수 불일치를 감지해 알린다. 구간 처리 지연 통계를 낸다.
- 하지 않는다: 프로파일 수학(MOD-08), 축 상태기·명령 검사(MOD-09), 입력 판정(MOD-11).

## 2. 의존성
- 사용: MOD-02 hal_pulse, MOD-08 motion_planner(순수 함수), MOD-01(시간·임계 구역).
- 사용하는 쪽: MOD-09 axis_control, MOD-10 estop_safety, MOD-11 io_manager(리밋 즉시 정지·원점 래치), MOD-13(진단값).

## 3. 입출력 정의
| 구분 | 이름 | 타입/단위 | 범위 | 설명 |
|---|---|---|---|---|
| 입력 | move | mp_move_t(MOD-08) | | 시작 위치·목표·속도·가감속·곡선 또는 JOG 지정 |
| 입력 | stop 요청 | enum | DECEL / QUICK | 감속 정지 / 즉시 정지(hal_pulse_abort) |
| 입력 | seg_target_us | uint32_t, µs | 기본 1,000 [가정] | 구간 목표 길이 |
| 출력 | position | int32_t, pulse | S32 | 지령 위치(하드웨어 계수 기반) |
| 출력 | speed | int32_t, pps | | 현재 구간 주파수(부호 = 방향) |
| 출력 | 이벤트 | pe_evt_t | DONE, STOPPED, UNDERRUN, HW_FAULT | L1 → L2 잠금 없는 링 버퍼로 전달 |
| 출력 | stats | 구조체 | underrun_count, max_isr_us, max_seg_latency_us | interface-spec §5.7 |
| 오류 | pe_err_t | | PE_EBUSY, PE_EINVAL, PE_ESTATE | |

공개 API(시그니처 수준):
```c
typedef enum { PE_STOP_DECEL, PE_STOP_QUICK } pe_stop_t;
typedef enum { PE_EVT_DONE, PE_EVT_STOPPED, PE_EVT_UNDERRUN, PE_EVT_HW_FAULT } pe_evt_kind_t;
typedef struct { uint8_t axis; pe_evt_kind_t kind; int32_t position; uint32_t t_us; } pe_evt_t;

pe_err_t pe_init(uint8_t axis, uint8_t hal_ch, uint32_t seg_target_us);
pe_err_t pe_configure(uint8_t axis, const hal_pulse_cfg_t *cfg, uint16_t dir_setup_us);  /* idle only */
pe_err_t pe_start(uint8_t axis, const mp_move_t *move);           /* idle only */
void     pe_request_stop(uint8_t axis, pe_stop_t how, uint32_t decel_rate); /* ISR-safe */
int32_t  pe_position(uint8_t axis);                                /* ISR-safe, atomic */
int32_t  pe_speed(uint8_t axis);
pe_err_t pe_set_position(uint8_t axis, int32_t pos);               /* idle only */
bool     pe_busy(uint8_t axis);
bool     pe_poll_event(pe_evt_t *out);                             /* L2 side, lock-free ring */
void     pe_get_stats(uint8_t axis, pe_stats_t *out);
```

동작(의사코드):
```
pe_start(axis, move):
    mp_init(&plan[axis], move)
    hal_pulse_set_dir(ch, sign(move))      ; wait dir_setup_us (busy-wait or one-shot timer)
    seg0 = to_hw(mp_next(&plan, seg_target_us)); seg1 = to_hw(mp_next(...))
    hal_pulse_start(ch, seg0); hal_pulse_push(ch, seg1)

ISR on HAL_PULSE_EVT_SEG_DONE(ch):            /* L1, HAL_FAST_CODE */
    t0 = hal_time_us()
    if stop_req == DECEL: mp_request_stop(&plan, decel_rate); stop_req = NONE
    s = mp_next(&plan, seg_target_us)
    if s.count > 0: hal_pulse_push(ch, to_hw(s))
    else: mark "last segment queued"         /* hardware stops by itself after last count */
    stats.max_isr_us = max(stats.max_isr_us, hal_time_us() - t0)

ISR on HAL_PULSE_EVT_UNDERRUN(ch):
    if plan finished: post DONE(position)    /* normal end: no next segment */
    else: post UNDERRUN(position); underrun_count++   /* deadline miss: stopped without extra pulses */

to_hw(s): period_ticks = round(tick_hz / s.freq_pps), clamp [min_ticks, max_ticks]; count = s.count
position = base_pos + dir * (hal_pulse_emitted(ch) - emitted_at_start)
```

## 4. 제약 조건
- 마감: 다음 구간 투입은 현재 구간이 끝나기 전에 끝나야 한다. 구간 목표 길이 1 ms [가정]이므로 ISR 처리 + 진입 지연 ≤ 1 ms − 여유. ISR 처리 시간 예산 ≤ 20 µs/축 [가정](2축 합계가 이더넷 ISR과 겹쳐도 마감 충족). 측정값은 stats로 공개.
- 펄스 수 정확도: 위치는 `hal_pulse_emitted`(하드웨어 계수)만으로 계산한다. 소프트웨어가 "보냈다고 생각한" 수를 위치로 쓰지 않는다(NFR-004). 구간 합계 = 이동 거리(MOD-08 보장)이고, 계수 누계가 계획 합계와 다르면 `PE_EVT_HW_FAULT`.
- 실행 문맥: SEG_DONE·UNDERRUN 처리 = L1 ISR(최우선). `pe_poll_event` = L2 태스크. ISR에서 RTOS API를 쓰지 않는다(MOD-01 §4).
- 연산: 정수(고정소수점)만 사용 [가정](FPU 유무·ISR 문맥 부동소수점 저장 비용 회피, 포트 간 결과 동일성).
- 축 독립: 축별 상태를 완전히 분리한다(FR-003). 한 축의 정지·오류가 다른 축의 구간 공급을 막지 않는다.
- 안전: `pe_request_stop(QUICK)`은 `hal_pulse_abort`를 즉시 호출(비상정지·리밋 경로). 비상정지 시 위치 처리 정책은 MOD-10.
- 메모리: 동적 할당 없음. 상태는 정적 배열(축 수 상한 2).

## 5. 완료 기준 (각 항목은 단위 테스트로 검증 가능해야 함)
| # | 기준 | 검증 방법 |
|---|---|---|
| 1 | 임의 이동(거리 1~10^8 pulse, 속도·가감속 무작위 1만 건)에서 투입 구간 합 = 거리, 마지막 이벤트 DONE | 호스트 시뮬(가짜 hal_pulse) |
| 2 | 구간 주파수가 `max_speed`를 넘지 않음 | 호스트 시뮬 |
| 3 | 가짜 hal_pulse에서 투입 지연을 주면 UNDERRUN 이벤트, 위치 = 실제 계수 | 호스트 시뮬 |
| 4 | 2축 동시 200 kpps 이동 반복, 외부 카운터 = 지령 거리(오차 0) | HIL T-01·T-02 |
| 5 | TCP 최대 부하·플래시 쓰기 중 underrun_count = 0, max_isr_us 기록 | HIL T-03·T-04 |
| 6 | 감속 정지 요청 시 감속 곡선 후 정지, 즉시 정지 시 abort 후 추가 펄스 ≤ MOD-02 기준 | HIL |
| 7 | 한 축 운전 중 다른 축 기동·정지 시 운전 축 주파수·펄스 수 영향 없음 | HIL(FR-003) |

## 6. 미확인·가정
- 구간 목표 길이 1 ms, ISR 예산 20 µs/축 [가정] → T-03 측정 후 조정.
- 방향 설정 대기(`dir_setup_us`) 구현 방식(바쁜 대기 vs 원샷 타이머)은 ④에서 결정.
- 32비트 위치 범위 초과(장시간 한 방향 JOG) 처리: 포화 후 알람 [가정].
