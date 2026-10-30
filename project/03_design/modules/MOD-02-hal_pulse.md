# 설계→코딩 인터페이스: hal_pulse (펄스 생성 HAL)

| 모듈 ID | 버전 | 작성 | 대상 언어/플랫폼 | 추적 SRS ID |
|---|---|---|---|---|
| MOD-02 | 0.1 | architect | C — port_stm32f4(TIM1/TIM8 + 32비트 계수 타이머 후보), port_esp32(RMT + PCNT) | FR-001, FR-002, NFR-001, NFR-002, NFR-003, NFR-004, NFR-005 |

## 1. 책임 (이 모듈이 하는 일 / 하지 않는 일)
- 한다: 채널(축)별로 "구간(주기 틱, 펄스 수)"을 하드웨어로 출력한다. 구간 큐(최소 2칸: 현재 + 다음)를 관리하고, 출력한 펄스 수를 하드웨어로 계수해 누계를 제공한다. Step/Dir·CW/CCW 모드, 펄스 논리, 방향 레벨을 설정한다. 즉시 정지(abort)를 제공한다.
- **불변식(모든 포트 필수)**: (1) 큐에 넣은 구간의 펄스 수 합과 실제 출력 펄스 수가 같다. (2) 다음 구간이 없을 때 현재 구간이 끝나면 **추가 펄스 없이 정지**하고 `HAL_PULSE_EVT_UNDERRUN`을 알린다(버퍼 되감기·반복 출력 금지). (3) 방향은 정지 중에만 바뀐다.
- 하지 않는다: 가감속 계산(MOD-08), 구간 분할(MOD-07), 위치 관리(MOD-07), 비상정지 판단(MOD-10). 하드웨어 비상정지 게이트는 회로 몫이며 이 모듈과 무관하게 동작한다.

## 2. 의존성
- 사용: MOD-01 hal_sys, 포트별 주변장치 드라이버(STM32 LL 타이머 / ESP-IDF RMT·PCNT).
- 사용하는 쪽: MOD-07 pulse_engine(유일).

## 3. 입출력 정의
| 구분 | 이름 | 타입/단위 | 범위 | 설명 |
|---|---|---|---|---|
| 입력 | ch | uint8_t | 0~(HAL_PULSE_MAX_CH−1), 2축 모델 0·1 | 축 = 채널 |
| 입력 | seg.period_ticks | uint32_t, 틱 | 포트별 [min_ticks, max_ticks] (`hal_pulse_caps`) | 펄스 주기. 200 kpps @168 MHz = 840틱 [산술] |
| 입력 | seg.count | uint32_t, pulse | 1~포트별 상한 | 구간 펄스 수 |
| 입력 | cfg.mode | enum | STEP_DIR / CW_CCW | FR-001, FR-002 |
| 입력 | cfg.active_low | bool | | 펄스 논리(NFR-005 드라이브 정합) |
| 입력 | dir | int8_t | +1 / −1 | Step/Dir: DIR 레벨, CW/CCW: 출력 채널 선택 |
| 출력 | hal_pulse_emitted(ch) | uint32_t, pulse | 순환 | 시작 이후 하드웨어 계수 누계 |
| 출력 | 이벤트 콜백 | enum | SEG_DONE, UNDERRUN, ABORTED | ISR 문맥 |
| 출력 | caps | 구조체 | | tick_hz, min/max_ticks, max_count, queue_depth |
| 오류 | hal_err_t | | HAL_EBUSY(큐 가득/운전 중 설정), HAL_EINVAL(범위), HAL_EIO(계수 불일치) | |

공개 API(시그니처 수준):
```c
typedef enum { HAL_PULSE_STEP_DIR = 0, HAL_PULSE_CW_CCW = 1 } hal_pulse_mode_t;
typedef struct { hal_pulse_mode_t mode; bool active_low; bool dir_invert; } hal_pulse_cfg_t;
typedef struct { uint32_t period_ticks; uint32_t count; } hal_pulse_seg_t;
typedef struct { uint32_t tick_hz, min_ticks, max_ticks, max_count; uint8_t queue_depth; } hal_pulse_caps_t;
typedef enum { HAL_PULSE_EVT_SEG_DONE, HAL_PULSE_EVT_UNDERRUN, HAL_PULSE_EVT_ABORTED } hal_pulse_evt_t;
typedef void (*hal_pulse_cb_t)(uint8_t ch, hal_pulse_evt_t evt, void *ctx);   /* ISR context */

hal_err_t hal_pulse_init(uint8_t ch, hal_pulse_cb_t cb, void *ctx);
void      hal_pulse_caps(uint8_t ch, hal_pulse_caps_t *out);
hal_err_t hal_pulse_config(uint8_t ch, const hal_pulse_cfg_t *cfg);   /* idle only */
hal_err_t hal_pulse_set_dir(uint8_t ch, int8_t dir);                  /* idle only */
hal_err_t hal_pulse_start(uint8_t ch, const hal_pulse_seg_t *first);  /* idle only */
hal_err_t hal_pulse_push(uint8_t ch, const hal_pulse_seg_t *next);    /* ISR-safe, HAL_EBUSY if full */
uint8_t   hal_pulse_free_slots(uint8_t ch);                           /* ISR-safe */
void      hal_pulse_abort(uint8_t ch);                                /* ISR-safe, immediate */
uint32_t  hal_pulse_emitted(uint8_t ch);                              /* ISR-safe */
bool      hal_pulse_busy(uint8_t ch);
```

포트 구현 개요(의사코드):
```
port_stm32f4 (C1):
  PWM timer (TIM1 / TIM8): CH1 = PULSE/CW, CH2 = DIR/CCW. ARR/CCR preload enabled.
  Count timer (32-bit, e.g. TIM2 / TIM5): clocked by PWM timer TRGO via ITRx (RM0090 table check, R-02).
  start: load seg0 -> ARR, CCR=ARR/2; count compare = seg0.count; enable.
  on count-compare IRQ: if next queued -> write preload ARR/CCR and new compare (cumulative); emit SEG_DONE
                        else -> stop PWM at end of current period (hardware: one-pulse/gating, method TBD by T-01/T-02); emit UNDERRUN
  CW/CCW: enable only CH1 or CH2 output (CCxE); STEP_DIR: CH2 forced level.
port_esp32 (C2):
  RMT channel per axis, symbols {high_ticks, low_ticks} generated per segment into ping-pong memory;
  end-of-data marker after last queued pulse (no wrap-around). PCNT counts the output pin for cross-check.
```

## 4. 제약 조건
- 타이밍: 200 kpps(주기 5 µs) 이상을 2채널 동시 출력(NFR-001·002). 펄스 사이에 CPU 개입이 없어야 한다(펄스당 인터럽트 금지, architecture P1 배제).
- 구간 경계: 구간 사이 주기 전환은 펄스 경계에서 일어나야 하며 경계에서 펄스 폭 왜곡·누락이 없어야 한다(preload 사용).
- 콜백·`push`는 ISR에서 실행. 콜백 처리 시간 예산은 MOD-07 몫이며 이 모듈은 콜백 진입 지연을 측정해 진단값으로 제공 [가정].
- 정지 정확도: 정지는 하드웨어 계수 비교로 결정한다(언더런 시에도). 소프트웨어 지연이 펄스 수에 영향을 주면 안 된다(NFR-004).
- 포트별 L1 코드는 `HAL_FAST_CODE`로 RAM 배치(플래시 쓰기 중 실행, R-13).
- 하드웨어 자원 배정(타이머·핀)은 interface-spec §2.1을 따르며 R-02 검사 결과로 바뀔 수 있다. API는 바뀌지 않는다.
- 안전: 비상정지 하드웨어 게이트가 출력단에서 차단해도 이 모듈의 계수는 계속될 수 있다(게이트가 출력단 뒤) → 위치 처리는 MOD-10 정책.

## 5. 완료 기준 (각 항목은 단위 테스트로 검증 가능해야 함)
| # | 기준 | 검증 방법 |
|---|---|---|
| 1 | 구간 1개(N = 1, 2, 1,000, 1,000,000) 출력 후 외부 카운터 값 = N, `hal_pulse_emitted` = N | HIL(MOD-22, 로직 분석기/카운터) |
| 2 | 2채널 동시 200 kpps 연속 10 s 이상, 축별 주파수 ≥ 200 kHz | HIL T-01 |
| 3 | 구간 큐를 비우면(언더런) 추가 펄스 0, UNDERRUN 이벤트 1회 | HIL(의도적 push 지연) |
| 4 | 구간 전환 1만 회에서 경계 펄스 폭이 설정 주기 대비 이상 없음(측정값 기록) | HIL(로직 분석기) |
| 5 | CW/CCW 모드에서 방향별로 지정 단자에만 펄스, Step/Dir 모드에서 DIR 레벨 정상, 극성 반전 동작 | HIL |
| 6 | `abort` 후 추가 펄스 ≤ 1(진행 중 펄스 완료 허용 여부는 측정 후 확정) | HIL |
| 7 | 운전 중 `config`/`set_dir`/`start` 호출 시 HAL_EBUSY | 단위 테스트(포트 모의) + HIL |

## 6. 미확인·가정
- STM32F407 타이머 내부 트리거 연결(TIM1→TIM2, TIM8→TIM5), 32비트 타이머 품목, 언더런 시 하드웨어 정지 방법(원펄스/게이트 슬레이브 모드 조합)은 RM0090 미대조(미확인) → T-01·T-02 시제에서 확정.
- ESP32 RMT 길이 필드 비트 수·핑퐁 세부는 TRM 원문 미확인(architecture S2).
- 구간 큐 깊이 2 [가정]. 마감 여유가 부족하면 늘린다.
- 펄스 최소 주파수(16비트 ARR + 분주 변경 필요 여부)는 포트 설계 후 산정(미확인).
