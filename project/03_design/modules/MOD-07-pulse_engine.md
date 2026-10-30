# 설계→코딩 인터페이스: pulse_engine (속도 갱신·위치 계수·정지 관리)

| 모듈 ID | 버전 | 작성 | 대상 언어/플랫폼 | 추적 SRS ID |
|---|---|---|---|---|
| MOD-07 | 0.2 | architect | C(MCU 무관, MOD-02 위) | FR-001, FR-003, FR-006, FR-007, FR-009, FR-010, NFR-001, NFR-002, NFR-003, NFR-004 |

> v0.2 개정: "구간 큐 공급" 모델을 MOD-02 v0.2(연속 PWM + 감시점 정지)에 맞춰 **주기적 속도 갱신 모델**로 바꿨다. 기존 servo1axis `motion_task`의 "현재 계수 → 프로파일 주파수 → 주기 설정" 루프[E2]를 재구성한다. 분류: **수정 후 재사용**.

## 1. 책임 (이 모듈이 하는 일 / 하지 않는 일)
- 한다: 축별로 이동을 시작(`hal_pulse_run`에 목표 수 전달)하고, 고정 주기 `upd_period_us`(기본 1,000 µs [가정], 기존 10 ms[E2])마다 두 축의 PCNT 계수를 읽어 MOD-08의 속도식으로 다음 주파수를 정해 `hal_pulse_set_freq`로 반영한다. 지령 위치(기준 위치 + 방향 × 계수)를 원자적으로 제공한다. 감속 정지·즉시 정지 요청을 처리한다. 갱신 지연·목표 초과를 감지해 알린다. 불변식 "목표 도달 시 주파수 ≤ f_stop_max"를 보장한다.
- 하지 않는다: 프로파일 수학(MOD-08), 축 상태기(MOD-09), 입력 판정(MOD-11), 하드웨어 설정(MOD-02).

## 2. 의존성
- 사용: MOD-02 hal_pulse, MOD-08 motion_planner(순수 함수), MOD-01(하드웨어 타이머 콜백·시간·임계 구역).
- 사용하는 쪽: MOD-09, MOD-10, MOD-11, MOD-13.

## 3. 입출력 정의
| 구분 | 이름 | 타입/단위 | 범위 | 설명 |
|---|---|---|---|---|
| 입력 | move | mp_move_t | | 거리(상대) 또는 목표(절대), 이동 속도, 시작 속도(0 = 자동), 가·감속 시간 |
| 입력 | stop 요청 | enum | DECEL / QUICK | |
| 입력 | upd_period_us | uint32_t, µs | 500~10,000 [가정] | 속도 갱신 주기 |
| 입력 | f_stop_max | uint32_t, pps | T-08 결과(미확인) | 목표 도달 허용 최고 주파수. 임시 5,000 [가정: 기존 자동 시작 속도 상한] |
| 출력 | position | int32_t, pulse | S32 | 지령 위치(계수 기반) |
| 출력 | move_pulses_done | uint32_t | | 이번 이동 출력 수(레거시 `legacy_current`) |
| 출력 | speed | int32_t, pps | | `hal_pulse_freq_actual` × 방향 |
| 출력 | 이벤트 | pe_evt_t | DONE, STOPPED, UPD_LATE, OVERSHOOT | 잠금 없는 링 버퍼 → L2 |
| 출력 | stats | | upd_late_count, upd_max_us, isr_lat_max_ns(MOD-02) | interface-spec §5.7 `underrun_count`·`max_seg_latency_us`에 매핑 |
| 오류 | pe_err_t | | PE_EBUSY, PE_EINVAL, PE_ESTATE | 시작 속도 > f_stop_max → PE_EINVAL |

공개 API(시그니처 수준):
```c
typedef enum { PE_STOP_DECEL, PE_STOP_QUICK } pe_stop_t;
typedef enum { PE_EVT_DONE, PE_EVT_STOPPED, PE_EVT_UPD_LATE, PE_EVT_OVERSHOOT } pe_evt_kind_t;
typedef struct { uint8_t axis; pe_evt_kind_t kind; int32_t position; uint32_t t_us; } pe_evt_t;

pe_err_t pe_init(uint8_t axis_count, uint32_t upd_period_us, uint32_t f_stop_max);
pe_err_t pe_start(uint8_t axis, const mp_move_t *move);              /* idle only */
void     pe_request_stop(uint8_t axis, pe_stop_t how);               /* ISR-safe */
int32_t  pe_position(uint8_t axis);                                   /* ISR-safe */
uint32_t pe_move_done(uint8_t axis);
int32_t  pe_speed(uint8_t axis);
pe_err_t pe_set_position(uint8_t axis, int32_t pos);                  /* idle only */
bool     pe_busy(uint8_t axis);
bool     pe_poll_event(pe_evt_t *out);
void     pe_get_stats(uint8_t axis, pe_stats_t *out);
```

동작(의사코드):
```
pe_start(axis, move):
    plan[axis] = mp_plan(move)                      /* MOD-08: accel/decel pulses, peak, start/stop freq */
    require plan.stop_hz <= f_stop_max
    hal_pulse_set_dir(ch, sign); wait dir_setup_us
    hal_pulse_run(ch, plan.start_hz, plan.distance)

update callback every upd_period_us (hardware timer, IRAM, both axes):     /* L1 */
    t0 = now; if t0 - last > 2 * upd_period_us: upd_late_count++, post UPD_LATE (once per move)
    for axis busy:
        n = hal_pulse_emitted(ch)
        f = (stop_req == DECEL) ? mp_decel_freq(&plan, n, t) : mp_freq_at(&plan, n)
        hal_pulse_set_freq(ch, f)
    upd_max_us = max(upd_max_us, now - t0)

on TARGET_REACHED(ch) (from MOD-02 ISR): mark done; post DONE(position) after overshoot check
on STOP (QUICK): hal_pulse_abort(ch); post STOPPED
on DECEL stop: plan switches to decel-from-current-speed, target = n + decel_pulses (re-arm watch point via hal_pulse API, ④ design)
position = base_pos + dir * hal_pulse_emitted(ch)
```

## 4. 제약 조건
- 마감: 갱신 콜백 처리 ≤ 미확인 µs(2축 합계), 지연 > 2 × upd_period_us를 "갱신 지연"으로 본다 [가정]. 갱신 지연은 펄스 수를 바꾸지 않으며(목표 정지는 하드웨어 감시점), 감속이 늦어져 목표 도달 주파수가 높아질 위험만 있다 → 감속 구간에서 갱신 지연이 나면 `mp_freq_at`이 남은 거리 기준으로 다시 계산하므로 다음 갱신에서 보정된다(기존 식이 위치 기반[E2]).
- 목표 도달 주파수: 위치 기반 감속식의 마지막 값 = 정지 주파수(= 시작 주파수). 이 값 ≤ f_stop_max를 시작 시 검사한다.
- 연산: 정수·고정소수점 [가정](기존 double·sqrt[E2]는 호스트 시험으로 결과 동등성 확인 후 교체). sqrt는 정수 근사(MOD-08).
- 실행 문맥: 갱신 콜백 = L1(IRAM, RTOS API 금지). 이벤트 소비 = L2 태스크.
- 축 독립(FR-003): 한 축 정지·오류가 다른 축 갱신을 막지 않는다.
- 메모리: 정적 배열(축 2).

## 5. 완료 기준 (각 항목은 단위 테스트로 검증 가능해야 함)
| # | 기준 | 검증 방법 |
|---|---|---|
| 1 | 무작위 이동 1만 건(거리 1~10^8, 속도·가감속 무작위)에서 가짜 hal_pulse 출력 합 = 거리, 마지막 주파수 = 정지 주파수 | 호스트 시뮬 |
| 2 | 주파수가 이동 속도를 넘지 않음, 갱신 지연을 주입해도 출력 합 = 거리 | 호스트 시뮬 |
| 3 | 기존 `calculate_profile`/`profile_frequency`(double)와 정수 구현의 주파수 차 ≤ 미확인 %(기준은 ④에서 제안) | 호스트 비교 시험 |
| 4 | 2축 동시 200 kpps 반복 이동, 외부 계수 = 거리 | HIL(T-04) |
| 5 | TCP 부하·플래시 쓰기 중 upd_late_count, upd_max_us 기록, 펄스 오차 0 | HIL(T-05·T-06) |
| 6 | 한 축 운전 중 다른 축 기동·정지 시 운전 축 영향 없음 | HIL |
| 7 | 시작 속도 > f_stop_max → PE_EINVAL | 단위 테스트 |

## 6. 미확인·가정
- upd_period_us 1,000 [가정], 갱신 지연 판정 2배 [가정] → T-05 측정 후 조정.
- f_stop_max 임시 5,000 pps [가정] → T-08 결과로 확정.
- 갱신 콜백 구현(GPTimer ISR vs esp_timer ISR 디스패치)은 ④에서 선택.
- 감속 정지 시 감시점 재설정 방식은 미확인 → TFW-02에서 확인. 기존 코드 주석에 "새 감시점은 계수 clear 후 하드웨어에 반영"이라고 되어 있어[E1], 운전 중 재설정은 계수 손실 위험이 있다. 대안: 감시점은 30,000 경계만 쓰고 감속 정지 목표는 갱신 콜백이 남은 펄스를 보고 정지 주파수에 도달한 뒤 즉시 정지(QUICK)하는 방식 [가정] — 이 경우 목표 정확도 요구가 없는 STOP 명령에만 적용한다.
