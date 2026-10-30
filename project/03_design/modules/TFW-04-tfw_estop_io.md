# 설계→코딩 인터페이스: tfw_estop_io (G3 전 시험 펌웨어 — GPIO19 비상정지·입력 지연)

| 모듈 ID | 버전 | 작성 | 대상 언어/플랫폼 | 추적 SRS ID |
|---|---|---|---|---|
| TFW-04 | 0.1 | architect | C(ESP-IDF 5.5.3, ESP32-P4) | FR-010, NFR-011, NFR-008, T-07, T-12 |

> 기존 1축에는 래치형 비상정지가 없다(STOP = 래더 100 ms 디바운스 후 강제 정지/리셋)[E4][E5]. 이 모듈은 신규 시험 구현이다. 입력 핀은 **GPIO19**(D-027). 커밋 2135568 코드는 GPIO18을 쓰므로(architecture R-14) 기동 시 핀 레벨을 로그로 남겨 배선을 확인한다.

## 1. 책임 (이 모듈이 하는 일 / 하지 않는 일)
- 한다:
  1. 경로 A(소프트웨어): GPIO19 엣지 인터럽트(IRAM, 디바운스 없음) → 두 축 force level + 타이머 정지(TFW-02 `stop`) → 래치. 래치 중 시험 명령 거부. 해제 = 입력 정상 + 시험 창 해제 명령.
  2. 경로 B(MCU 내 하드웨어): GPIO19를 MCPWM 그룹 0·1의 GPIO 고장 입력으로 연결해 오퍼레이터 브레이크(출력 Low). 생성 실패 시 원인 기록(자원 확인).
  3. 경로 선택: 시험 창에서 A만 / B만 / A+B.
  4. 입력 지연 측정 모드(T-12): GPIO19(또는 지정 입력 핀) 엣지 → ISR 진입 시각 기록, 디버그 GPIO 토글로 외부 측정 보조.
  5. 극성 설정: 기존 Active-LOW·풀업[E5] / 반전(NC 접점 전제, DC-17) 두 가지를 빌드 설정으로.
- 하지 않는다: 외부 하드웨어 게이트(회로, T-07 시제가 있을 때만 측정 대상), 제품 래치 규칙 전체(MOD-10).

## 2. 의존성
- 사용: ESP-IDF GPIO·MCPWM 고장(fault)·브레이크 API, TFW-02(정지 호출, 오퍼레이터 핸들 공유).
- 사용하는 쪽: TFW-01, TFW-05.

## 3. 입출력 정의
| 구분 | 이름 | 타입/단위 | 범위 | 설명 |
|---|---|---|---|---|
| 입력 | TFW_ESTOP_GPIO | 빌드 설정 | 19 | D-027 |
| 입력 | TFW_ESTOP_ACTIVE_LOW | 빌드 설정 | 1(기존) / 0 | DC-17 비교 |
| 입력 | 0x7060 | es_path_sel | U16 | 1 A, 2 B, 3 A+B | |
| 입력 | 0x7061 | es_release | U16 | 1 = 해제 요청 | 입력 활성 중이면 거부 |
| 출력 | 0x7062 | es_state | U16 | 0 정상, 1 비상정지 중, 2 래치(해제 대기) | |
| 출력 | 0x7063 | es_trip_count | U16 | | |
| 출력 | 0x7064 | es_isr_lat_max_ns | U32 | 엣지 → ISR 진입(사이클 카운터 기준 추정) | |
| 출력 | 0x7066 | es_pulses_after_trip_ax1 / 0x7068 _ax2 | U32 | 트립 시점 PCNT 대비 최종 PCNT 차 | 경로별 추가 펄스 |
| 출력 | 0x706A | es_fault_bind_ok | U16 | 0 실패, 1 그룹 0만, 3 두 그룹 | 자원 확인 |
| 출력 | 0x706B | es_boot_level | U16 | 부팅 시 GPIO19 레벨 | 배선 확인(R-14) |
| 오류 | 0x706C | es_last_err | U16 | esp_err_t 하위 16비트 | |

공개 API(시그니처 수준):
```c
esp_err_t tfw_estop_init(int gpio, bool active_low, mcpwm_oper_handle_t oper[2]);
void      tfw_estop_set_path(uint8_t sel);
uint16_t  tfw_estop_release(void);
bool      tfw_estop_latched(void);
void      tfw_estop_stats(tfw_es_stats_t *out);
```

동작(의사코드):
```
init: gpio input, pull per polarity, intr on active edge (IRAM handler)
      for g in 0,1: fault[g] = mcpwm_new_gpio_fault(gpio, active level) ; oper brake on fault (one-shot), gen brake action LOW
      record bind result ; log boot level
ISR (path A): snapshot PCNT ax1/ax2 ; tfw_pulse2_stop(all) ; latched = 1 ; trip_count++
after 10 ms (task): pulses_after_trip = PCNT_now - snapshot
release: if input active -> reject ; clear one-shot brake ; latched = 0 (no auto restart)
```

## 4. 제약 조건
- 디바운스 없음(첫 엣지 반응). 해제 판정만 10 ms 안정 확인 [가정].
- 래치 중 TFW-01 이동 명령은 거부(tst_status 0x10).
- 운전 주파수 200 kHz·1 MHz에서 각 경로 100회 [가정] 측정(TFW-05가 디지털 출력으로 GPIO19를 구동하거나 수동 스위치 사용).

## 5. 완료 기준 (각 항목은 단위 테스트로 검증 가능해야 함)
| # | 기준 | 검증 방법 |
|---|---|---|
| 1 | 경로 A·B·A+B 각각 GPIO19 엣지 → 두 축 마지막 펄스까지 시간 분포 기록(로직 분석기) | HIL(T-07) |
| 2 | 래치 중 이동 명령 거부, 입력 활성 중 해제 거부, 해제 후 자동 재기동 없음 | HIL |
| 3 | es_fault_bind_ok 값과 실패 원인 기록 | HIL 부팅 로그 |
| 4 | 입력 지연 측정 모드에서 엣지 → ISR 지연 분포 기록 | HIL(T-12) |
| 5 | es_pulses_after_trip 경로별 기록(DC-08 근거) | HIL(T-07) |

## 6. 미확인·가정
- MCPWM 고장 입력 수, 한 GPIO를 두 그룹 고장 입력에 동시 연결 가능 여부, 브레이크 API 동작 — 미확인(이 모듈로 확인).
- 외부 게이트 시제 회로 유무 미정(없으면 T-07은 경로 A·B만).
- 해제 안정 시간 10 ms, 반복 100회 [가정].
