# 설계→코딩 인터페이스: tfw_pulse2 (G3 전 시험 펌웨어 — 2축 MCPWM+PCNT 펄스)

| 모듈 ID | 버전 | 작성 | 대상 언어/플랫폼 | 추적 SRS ID |
|---|---|---|---|---|
| TFW-02 | 0.1 | architect | C(ESP-IDF 5.5.3, ESP32-P4) | NFR-001, NFR-002, NFR-003, NFR-004, FR-001, FR-002(자원 확인만), T-03~T-06, T-08~T-11, T-14 |

> servo1axis `motion_core`[E1]와 프로파일 함수 `calculate_profile`/`profile_frequency`[E2]를 복사해 2축으로 확장한 시험용 구현이다. MOD-02·07 제품 설계(v0.2)의 가설을 실측하는 것이 목적이며, 결과에 따라 MOD-02·07이 바뀔 수 있다.

## 1. 책임 (이 모듈이 하는 일 / 하지 않는 일)
- 한다: 축1 = MCPWM 그룹 0 + PCNT 유닛 A(GPIO25 루프백), 축2 = 그룹 1 + 유닛 B(핀 빌드 설정) 초기화. 해상도·속도 갱신 주기 설정. 이동(목표 펄스, 사다리꼴/삼각형, 시작 속도 = min(5,000, 속도)), 고정 주파수 연속 출력, 즉시 정지. 계측: 감시점 ISR 지연, 갱신 콜백 처리 시간·지연, 요청 대비 실제 주파수. 자원 탐색: 그룹 1 생성 가능 여부, 오퍼레이터당 제너레이터 2개 생성 가능 여부(CW/CCW), PCNT 유닛당 채널 2개 생성 가능 여부, 주기 최대 틱(생성 실패 경계). 선택 실험: PCNT → MCPWM ETM 연결 가능 여부.
- 하지 않는다: 절대 위치·원점·리밋, 제품 HAL API 완성(MOD-02 몫).

## 2. 의존성
- 사용: ESP-IDF MCPWM·PCNT·GPTimer(또는 esp_timer)·ETM(선택), TFW-04(고장 브레이크 연결 요청 시).
- 사용하는 쪽: TFW-01.

## 3. 입출력 정의
| 구분 | 이름 | 타입/단위 | 범위 | 설명 |
|---|---|---|---|---|
| 입력 | TFW_AX1_PLS/DIR/EN_GPIO | 빌드 설정 | 25 / 32 / 33 | 기존[E4] |
| 입력 | TFW_AX2_PLS/DIR/EN_GPIO | 빌드 설정 | 미확인 | PS-27 전에는 사용자 배선에 맞춤(후보: 비는 W5500 핀 GPIO2~6·14 [가정]) |
| 입력 | res_hz | uint32_t | 2 M/10 M/20 M/40 M [가정] | |
| 입력 | upd_us | uint32_t | 500~10,000 | 기존 10,000(태스크)[E2], 기본 1,000 |
| 입력 | upd_mode | enum | TASK_10MS(기존 재현) / HWTIMER | 기존 방식과 비교 |
| 입력 | freq, count, accel_ms, decel_ms | | TFW-01 범위 | |
| 출력 | done 이벤트, PCNT 값 | | | TFW-01 결과 카운터 |
| 출력 | stats | isr_lat_max_ns, upd_max_us, upd_late_count, freq_actual | | |
| 출력 | caps 탐색 결과 | group1_ok, gen2_ok, pcnt_ch2_ok, etm_pcnt_ok, max_period_ticks | | 부팅 로그 + 시험 창(④에서 주소 배정) |
| 오류 | esp_err_t | | | 자원 생성 실패 코드 기록 |

공개 API(시그니처 수준):
```c
esp_err_t tfw_pulse2_init(uint8_t axis_count, uint32_t res_hz, uint32_t upd_us, tfw_upd_mode_t mode);
esp_err_t tfw_pulse2_move(uint8_t axis, uint32_t speed_hz, uint32_t count, uint32_t accel_ms, uint32_t decel_ms);
esp_err_t tfw_pulse2_run_fixed(uint8_t axis, uint32_t freq_hz, uint32_t count /*0=endless*/);
void      tfw_pulse2_stop(uint8_t axis);                 /* force low + timer stop */
bool      tfw_pulse2_busy(uint8_t axis);
uint32_t  tfw_pulse2_count(uint8_t axis);
void      tfw_pulse2_stats(uint8_t axis, tfw_pulse_stats_t *out);
void      tfw_pulse2_probe_caps(tfw_caps_t *out);        /* resource probing at boot */
```

동작(의사코드):
```
init: for axis: copy of motion_init() with group_id = axis, resolution = res_hz,
      timer flag update_period_on_empty (if field exists), PCNT on same pin, on_reach IRAM
      start GPTimer(upd_us) callback (IRAM) when mode == HWTIMER, else 10 ms task (existing behaviour)
set_freq(f): ticks = ceil(res_hz / f), min 2 ; record freq_actual = res_hz / ticks
update(): for busy axis: n = count(axis); set_freq(profile_frequency_int(axis, n))
          record callback duration and inter-call jitter
on_reach ISR: t_isr = esp_cpu_get_cycle_count() ; force low ; stop timer ; record (t_isr - t_expected) [method: expected = time when count reached, estimated from last update, ④ to refine]
probe_caps: try mcpwm_new_timer(group 1) ; try 2nd generator on operator ; try 2nd PCNT channel ;
            try timer with period_ticks = 65536, 2^20 ... -> record max accepted ; try ETM channel PCNT->MCPWM (if API exists)
```

## 4. 제약 조건
- 펄스당 CPU 개입 없음. 감시점은 30,000 경계 + 목표 나머지(기존 방식).
- ISR·갱신 콜백은 IRAM 배치, RTOS API는 ISR용 함수만.
- 축1과 축2는 상태를 공유하지 않는다(기존 정적 변수 → 축 배열로).
- 측정 코드가 측정 대상 지연을 바꾸지 않도록 ISR 내부 계측은 사이클 카운터 읽기 2회 이내로 제한 [가정].

## 5. 완료 기준 (각 항목은 단위 테스트로 검증 가능해야 함)
| # | 기준 | 검증 방법 |
|---|---|---|
| 1 | 축2 생성 성공(그룹 1·PCNT 2유닛) — 실패 시 원인 기록 | HIL 부팅 로그 |
| 2 | 2축 동시 고정 200 kHz·500 kHz·1 MHz 10 s, 로직 분석기 주파수 기록 | HIL(T-03) |
| 3 | 2축 RUN_SEQ에서 PCNT = 목표, 외부 계수 = 목표 | HIL(T-04) |
| 4 | upd_mode TASK_10MS와 HWTIMER의 갱신 지연·가감속 계단 비교표 | HIL(T-09) |
| 5 | 해상도 4종별 freq_actual ≤ 요청, 최저·최고 주파수, 경계 펄스 폭 | HIL(T-09) |
| 6 | 감시점 ISR 지연 분포(무부하·TCP 부하·NVS 부하) | HIL(T-08) |
| 7 | probe_caps 결과(제너레이터 2, PCNT 채널 2, max_period_ticks, ETM) 기록 | HIL 부팅 로그 |

## 6. 미확인·가정
- 그룹·유닛·채널·주기 폭은 미확인 → 이 모듈의 probe로 확인한다(architecture §4.3).
- ISR 지연 측정 기준점(언제 목표에 도달했는가)의 정밀 측정은 외부 로직 분석기(감시점 ISR에서 디버그 GPIO 토글 [가정])로 보완한다. 디버그 GPIO는 빌드 설정, 핀 미확인.
- ETM API 존재 여부·PCNT ETM 지원 미확인(선택 실험, 결과가 없어도 시험 펌웨어 완료로 본다).
- 해상도 후보 값 [가정].
