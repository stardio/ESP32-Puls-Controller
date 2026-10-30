# 설계→코딩 인터페이스: io_manager (입력 판정·필터·빠른 정지 경로·출력)

| 모듈 ID | 버전 | 작성 | 대상 언어/플랫폼 | 추적 SRS ID |
|---|---|---|---|---|
| MOD-11 | 0.1 | architect | C(MCU 무관) | FR-008, FR-009, FR-011, FR-021, NFR-008 |

## 1. 책임 (이 모듈이 하는 일 / 하지 않는 일)
- 한다: 축별 입력(HOME, LIM+, LIM−, ALM, INP)에 접점 논리(`input_logic`)와 디지털 필터(`input_filter_ms`)를 적용해 활성 상태를 만든다. 빠른 경로: 리밋·드라이브 알람의 활성 엣지는 ISR에서 즉시 해당 축 정지를 요청한다(이동 방향 리밋만, FR-009). 원점 엣지 시 하드웨어 계수값 스냅샷(원점복귀용). 입력 변화 이벤트를 MOD-09에 전달. 진단용 원시·논리 입력 비트 제공(FR-021). 서보 ON 출력 쓰기.
- 하지 않는다: 비상정지 입력(MOD-10이 직접 처리), 축 상태 판단(MOD-09).

## 2. 의존성
- 사용: MOD-03, MOD-07(`pe_request_stop`, `pe_position`, `pe_speed`), MOD-12(입력 파라미터).
- 사용하는 쪽: MOD-09, MOD-13, MOD-14(진단 비트).

## 3. 입출력 정의
| 구분 | 이름 | 타입/단위 | 범위 | 설명 |
|---|---|---|---|---|
| 입력 | input_logic | BITS16 | interface-spec §5.6 | NO/NC |
| 입력 | input_filter_ms | U16, ms | 0~20 | 해제·저속 신호 필터 |
| 입력 | limit_stop_mode, alarm_stop_mode | enum | 즉시/감속 | |
| 출력 | io_evt_t | (axis, signal, active, t_us, latched_pos) | | 큐로 MOD-09에 |
| 출력 | axis_inputs | BITS16 | b0~b4 논리, b8~b12 원시 | interface-spec §5.4 +0x00A |
| 오류 | — | | | |

공개 API(시그니처 수준):
```c
typedef enum { IO_HOME, IO_LIMP, IO_LIMN, IO_ALM, IO_INP } io_sig_t;
typedef struct { uint8_t axis; io_sig_t sig; bool active; uint32_t t_us; int32_t latched_pos; } io_evt_t;

void     io_init(uint8_t axis_count);
void     io_apply_params(uint8_t axis);           /* after param change, axis idle */
void     io_task_tick(void);                      /* MOTION task 1 ms: filtering, slow signals */
bool     io_active(uint8_t axis, io_sig_t sig);
uint16_t io_axis_bits(uint8_t axis);
bool     io_poll_event(io_evt_t *out);
hal_err_t io_set_servo_on(uint8_t axis, bool on);
```

빠른 경로(의사코드):
```
ISR on raw edge (axis, sig):
    active = apply_logic(raw)
    if sig == LIMP and active and pe_speed(axis) > 0: pe_request_stop(axis, limit_stop_mode)
    if sig == LIMN and active and pe_speed(axis) < 0: pe_request_stop(axis, limit_stop_mode)
    if sig == ALM  and active: pe_request_stop(axis, alarm_stop_mode)
    if sig == HOME: latched_pos = pe_position(axis)
    push io_evt_t (lock-free ring)
1 ms tick: debounce for state reporting & release; INP sampled (no ISR)
```

## 4. 제약 조건
- 정지 반응은 필터 없이 첫 활성 엣지에서(안전 측). 해제·보고는 필터 적용. 노이즈로 인한 오정지 가능성은 T-08·EMC 시험에서 평가 [가정].
- 리밋 활성 방향 판정은 현재 이동 방향 기준. 정지 중 리밋 활성 → 그 방향 명령 거부는 MOD-09가 담당.
- ISR 처리 시간 ≤ 5 µs [가정].
- 원점 래치 정밀도 = 입력 회로 지연(T-08) + ISR 진입 지연 × 크리프 속도 [산술 관계]. 정밀도 목표값은 SRS에 없음(미확인).

## 5. 완료 기준 (각 항목은 단위 테스트로 검증 가능해야 함)
| # | 기준 | 검증 방법 |
|---|---|---|
| 1 | NO/NC 설정별 활성 판정 표 일치 | 호스트 시뮬 |
| 2 | 필터: 설정 시간 미만 펄스 입력은 보고 안 됨 | 호스트 시뮬 |
| 3 | +이동 중 LIM+ → 정지, LIM− 활성은 +이동에 영향 없음 | HIL |
| 4 | 리밋 엣지 → 마지막 펄스까지 시간 측정 기록(Q14 근거) | HIL |
| 5 | 원점 엣지 래치값 반복 측정(정밀도 기록) | HIL |
| 6 | 서보 ON 출력 단자 레벨 | HIL |

## 6. 미확인·가정
- NFR-008 판정 전압은 회로(TLP293-4 + 저항) 값으로 정해지며 T-08 측정 후 제안.
- 필터 기본 1 ms, ISR 예산 5 µs [가정].
