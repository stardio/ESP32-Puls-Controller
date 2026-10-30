# 설계→코딩 인터페이스: hal_pulse (펄스 생성 HAL)

| 모듈 ID | 버전 | 작성 | 대상 언어/플랫폼 | 추적 SRS ID |
|---|---|---|---|---|
| MOD-02 | 0.2 | architect | C — **port_esp32p4(ESP-IDF 5.5.3, MCPWM + PCNT)** 1차 / port_stm32f4는 architecture §4.5 전환 시만 | FR-001, FR-002, NFR-001, NFR-002, NFR-003, NFR-004, NFR-005 |

> v0.2 개정: 구간 큐(RMT/타이머) 모델을 기존 servo1axis `motion_core`[E1]의 **연속 PWM + 핀 루프백 계수 + 감시점 정지** 모델로 바꿨다. 분류: **수정 후 재사용**(architecture §3.2). servo1axis 원본은 수정하지 않고 복사해 이관한다.

## 1. 책임 (이 모듈이 하는 일 / 하지 않는 일)
- 한다: 채널(축)별로 MCPWM 그룹 1개를 할당해 펄스를 연속 출력하고, 같은 출력 핀을 PCNT 유닛으로 계수해 **출력 누계(U64 내부, 32비트 읽기)**를 제공한다. 주파수 설정(주기 틱 = 반올림, 요청 이하), 목표 펄스 수 도달 시 하드웨어 출력 강제 Low(감시점 ISR), 즉시 정지, Step/Dir·CW/CCW·논리 반전, MCPWM 고장 입력 브레이크 연결(비상정지용), 계측값(감시점 ISR 지연)을 제공한다.
- 불변식(포트 필수): (1) 위치 근거는 PCNT 계수뿐이다. (2) 목표 도달 이벤트 후 추가 계수 > 0이면 `HAL_PULSE_EVT_OVERSHOOT`. (3) 방향·모드 변경은 정지 중에만. (4) 주파수 설정은 요청값을 넘지 않는다(R-15).
- 하지 않는다: 가감속 계산(MOD-08), 속도 갱신 주기 관리(MOD-07), 위치 부호·기준(MOD-07), 비상정지 판단(MOD-10).

## 2. 의존성
- 사용: MOD-01 hal_sys, ESP-IDF `driver/mcpwm_prelude.h`, `driver/pulse_cnt.h`, GPIO.
- 사용하는 쪽: MOD-07 pulse_engine(주), MOD-10 estop_safety(`hal_pulse_bind_fault`, `hal_pulse_abort`).
- 기존 코드 재사용 대상[E1]: MCPWM 생성 순서(timer → operator → comparator(TEZ 갱신) → generator(TEZ High, 비교 일치 Low)), PCNT 상한 30,000 + 소프트웨어 누적, 감시점 추가 후 clear로 활성화, IRAM ISR에서 `mcpwm_generator_set_force_level(gen, 0, true)`.

## 3. 입출력 정의
| 구분 | 이름 | 타입/단위 | 범위 | 설명 |
|---|---|---|---|---|
| 입력 | ch | uint8_t | 0~1 | 축 = 채널. ch0 → MCPWM 그룹 0·PCNT 유닛 A, ch1 → 그룹 1·유닛 B |
| 입력 | cfg.pls_gpio / dir_gpio | int | GPIO 번호 | ch0 = 25 / 32[E4]. ch1 = 미확인(interface-spec §2.1) |
| 입력 | cfg.res_hz | uint32_t, Hz | 포트 허용값(미확인, T-09) | MCPWM 타이머 해상도. 기존 2,000,000[E4]. 후보 10/20/40 MHz [가정] |
| 입력 | cfg.mode | enum | STEP_DIR / CW_CCW | CW_CCW 자원 미확인 시 `HAL_ENOTSUP` |
| 입력 | cfg.active_low, dir_invert | bool | | |
| 입력 | freq_hz | uint32_t, pps | caps.min_hz ~ caps.max_hz | `hal_pulse_set_freq` |
| 입력 | target | uint64_t, pulse | 1 ~ 2^32−1(레거시 범위)[E6] | `hal_pulse_run` 목표 수(0 = 무한, JOG) |
| 출력 | emitted | uint32_t, pulse | 이번 run 기준 | PCNT 누계 |
| 출력 | 이벤트 콜백 | enum | TARGET_REACHED, OVERSHOOT, ABORTED | IRAM ISR 문맥 |
| 출력 | caps | 구조체 | res_hz, min_hz, max_hz, max_ticks | max_ticks(주기 레지스터 폭)는 기동 시 측정·기록(미확인) |
| 출력 | stats | 구조체 | isr_lat_max_ns, isr_count | 감시점 ISR 진입 지연(측정 방법: ISR 진입 시각 − 예상 도달 시각, ④에서 확정) |
| 오류 | hal_err_t | | HAL_EBUSY, HAL_EINVAL, HAL_ENOTSUP, HAL_EIO | |

공개 API(시그니처 수준):
```c
typedef enum { HAL_PULSE_STEP_DIR = 0, HAL_PULSE_CW_CCW = 1 } hal_pulse_mode_t;
typedef struct { int pls_gpio, dir_gpio; uint32_t res_hz; hal_pulse_mode_t mode;
                 bool active_low, dir_invert; } hal_pulse_cfg_t;
typedef struct { uint32_t res_hz, min_hz, max_hz, max_ticks; } hal_pulse_caps_t;
typedef enum { HAL_PULSE_EVT_TARGET_REACHED, HAL_PULSE_EVT_OVERSHOOT, HAL_PULSE_EVT_ABORTED } hal_pulse_evt_t;
typedef void (*hal_pulse_cb_t)(uint8_t ch, hal_pulse_evt_t evt, uint32_t emitted, void *ctx); /* IRAM ISR */

hal_err_t hal_pulse_init(uint8_t ch, const hal_pulse_cfg_t *cfg, hal_pulse_cb_t cb, void *ctx);
void      hal_pulse_caps(uint8_t ch, hal_pulse_caps_t *out);
hal_err_t hal_pulse_set_dir(uint8_t ch, int8_t dir);                 /* idle only */
hal_err_t hal_pulse_run(uint8_t ch, uint32_t start_hz, uint64_t target); /* idle only, target 0 = endless */
hal_err_t hal_pulse_set_freq(uint8_t ch, uint32_t freq_hz);          /* ISR/timer-callback safe, applied at TEZ */
uint32_t  hal_pulse_freq_actual(uint8_t ch);                         /* res_hz / period_ticks */
void      hal_pulse_abort(uint8_t ch);                               /* ISR-safe: force low + timer stop */
uint32_t  hal_pulse_emitted(uint8_t ch);                             /* ISR-safe */
bool      hal_pulse_busy(uint8_t ch);
hal_err_t hal_pulse_bind_fault(int gpio, bool active_high);          /* both groups, brake on fault (MOD-10) */
void      hal_pulse_get_stats(uint8_t ch, hal_pulse_stats_t *out);
```

포트 구현 개요(의사코드, port_esp32p4):
```
init(ch): group = ch; timer(res_hz, UP, period = res_hz/1000, update_period_on_empty = true [API field to confirm])
          operator -> comparator(update_cmp_on_tez) -> generator A on pls_gpio:
              TEZ -> HIGH, CMP -> LOW ; force LOW (idle)
          PCNT unit: high_limit 30000, low_limit -1, accum off ; channel edge = pls_gpio, rising INCREASE
          on_reach callback (IRAM, CONFIG_PCNT_ISR_IRAM_SAFE = y)
set_freq(f): ticks = ceil(res_hz / f)            /* never above requested f (R-15) */
             clamp [2, max_ticks]; set period; cmp = ticks/2
run(start_hz, target): remove watch points; acc = 0; add WP 30000; if target%30000: add WP target%30000
                       clear count; start PCNT; set_freq(start_hz); release force; timer START_NO_STOP
on_reach(wp): if wp == 30000: acc += 30000
              if acc + wp_part >= target: force LOW; timer STOP_EMPTY; post TARGET_REACHED
after stop (task side, MOD-07): if emitted > target -> OVERSHOOT
bind_fault(gpio): per group: mcpwm_new_gpio_fault(gpio) ; operator brake on fault (CBC/OST, choose OST) ; gen action on brake = LOW
CW_CCW: generator B on dir_gpio (if available) else re-route generator A output via GPIO matrix while idle; PCNT 2nd channel on dir_gpio
```

## 4. 제약 조건
- 타이밍: 축당 ≥ 200 kpps, 2채널 동시(NFR-001·002). 펄스당 CPU 개입 없음. 감시점 ISR은 30,000 펄스마다 1회 + 목표 1회(1 MHz에서 30 ms마다 [산술]).
- 목표 정지 정확도: 목표 도달 시 주파수 ≤ f_stop_max(MOD-07 보장). 이 모듈은 ISR 지연 통계를 제공해 f_stop_max 산정 근거를 낸다(T-08).
- 플래시 작업 중 동작: PCNT·MCPWM ISR과 콜백 코드·데이터는 IRAM/DRAM 배치(`CONFIG_PCNT_ISR_IRAM_SAFE`, `CONFIG_MCPWM_ISR_IRAM_SAFE` 또는 동등 설정, 기존 sdkconfig는 미설정[E8], R-16).
- PCNT 인터럽트는 전 유닛 공유[S12] → 2축 동시 도달 시 ISR 순차 처리. ISR 처리 시간 ≤ 미확인 µs(측정값 공개).
- 주기 갱신은 TEZ(주기 경계)에서 반영해 펄스 폭 왜곡을 막는다. 갱신 시점 API 동작은 T-09로 확인.
- 스레드 안전: `set_freq`는 MOD-07 속도 갱신 문맥 하나에서만 호출. `abort`·`emitted`는 어디서나.
- 안전: 고장 브레이크는 MCU 내 하드웨어 경로로, 펌웨어 루프 정지와 무관하게 출력 Low(자원 미확인 시 `HAL_ENOTSUP` 반환 + MOD-10이 ISR 경로만 사용).
- 메모리: 동적 할당은 init 시 ESP-IDF 드라이버 내부만.

## 5. 완료 기준 (각 항목은 단위 테스트로 검증 가능해야 함)
| # | 기준 | 검증 방법 |
|---|---|---|
| 1 | 목표 N = 1, 2, 1,000, 29,999, 30,000, 30,001, 1,000,000에서 외부 계수 = PCNT = N | HIL(로직 분석기) |
| 2 | 2채널 동시 200 kHz 10 s 이상, 두 채널 주파수 ≥ 200 kHz. 500 k·1 MHz 결과 기록 | HIL(T-03) |
| 3 | `set_freq(f)` 후 `freq_actual` ≤ f, 해상도별 오차표 기록 | 단위 테스트(틱 계산) + HIL(T-09) |
| 4 | 주기 갱신 1만 회에서 경계 펄스 폭이 직전·직후 주기 범위 안 | HIL(T-09) |
| 5 | 플래시 쓰기 반복 중 목표 정지 초과 0(IRAM 안전 설정 on) | HIL(T-06) |
| 6 | `bind_fault` 후 고장 입력 활성 → 두 채널 출력 Low, 지연 측정 | HIL(T-07) |
| 7 | 운전 중 `set_dir`·`run` → HAL_EBUSY | HIL |
| 8 | ch1 init이 그룹 1·두 번째 PCNT 유닛으로 성공(2축 자원 확인) | HIL 기동 시험 |

## 6. 미확인·가정
- 그룹당 타이머·오퍼레이터·제너레이터 수, PCNT 유닛·채널·감시점 수, 고장 입력 수, 주기 레지스터 폭(max_ticks) — ESP32-P4 TRM·soc_caps.h 원문 미대조(architecture §4.3). #8 기동 시험으로 필요 자원만 확인한다.
- `update_period_on_empty` 등 ESP-IDF 5.5.3 필드명은 ④에서 헤더로 확인.
- 해상도 후보 10/20/40 MHz [가정]. 최저 주파수 요구(기존 100 Hz[E2])와 분해능의 절충은 T-09 후 결정.
- PCNT ETM 이벤트로 하드웨어 정지 가능 여부 미확인(architecture §2.4). 가능하면 `on_reach` 정지를 ETM으로 대체하는 포트 옵션을 둔다.
- port_stm32f4(v0.1 의사코드)는 전환 시 v0.1 문서 이력을 참고해 다시 작성한다.
