# 설계→코딩 인터페이스: tfw_main (G3 전 시험 펌웨어 — 앱·시험 시퀀서·부하 태스크)

| 모듈 ID | 버전 | 작성 | 대상 언어/플랫폼 | 추적 SRS ID |
|---|---|---|---|---|
| TFW-01 | 0.1 | architect | C(ESP-IDF 5.5.3, ESP32-P4 / Waveshare ESP32-P4-ETH) | NFR-002, NFR-003, NFR-004, FR-015(간섭만), T-03~T-09, T-14 |

> D-025 예외 범위의 시험 펌웨어다. 제품 모듈이 아니며 G3 판단 근거(실측 데이터)만 만든다. 산출물 위치: `project/04_code/test-fw/`(D-025). servo1axis(커밋 2135568)는 **읽기 전용**이며 필요한 파일은 복사한다.

## 1. 책임 (이 모듈이 하는 일 / 하지 않는 일)
- 한다: 앱 진입(NVS 초기화 — 기존 `init_nvs`[E4] 복사), TFW-02·03·04 초기화, 시험 레지스터 창(0x7000~0x70FF)의 명령 해석과 시험 시퀀스 실행(반복 이동·무작위 이동·고정 주파수), 부하 태스크(NVS 쓰기 주기, CPU 부하) 제어, 부팅 로그(칩 리비전, CPU 클록, 해상도·갱신 주기·IRAM 설정, 핀 배정)와 결과 카운터 제공.
- 하지 않는다: 원점복귀·리밋·알람·파라미터 체계·RTU·펌웨어 갱신·제품 레지스터 맵(④ 범위). 래더 코드(`movion_ladder*`)는 포함하지 않는다(GPIO 극성 재생성 문제[E5]).

## 2. 의존성
- 사용: TFW-02 tfw_pulse2, TFW-03 tfw_modbus, TFW-04 tfw_estop_io, ESP-IDF(NVS, esp_chip_info, FreeRTOS).
- 사용하는 쪽: TFW-05 tfw_host(Modbus로 제어).

## 3. 입출력 정의
시험 레지스터 창(홀딩 레지스터, 32비트 하위 워드 먼저):

| 구분 | 주소 | 이름 | 타입/단위 | 범위 | 설명 |
|---|---|---|---|---|---|
| 입력 | 0x7000 | tst_cmd | U16 | 1 RUN_FIXED, 2 RUN_MOVE, 3 RUN_SEQ, 4 STOP_ALL, 5 CLEAR_STATS, 6 SET_LOAD | 쓰기 시 실행, 읽기 0 |
| 입력 | 0x7001 | tst_axis_mask | U16 | b0 축1, b1 축2 | 동시 실행 축 |
| 입력 | 0x7002 | tst_freq | U32, Hz | 100~1,000,000 | 고정 주파수 / 이동 속도 |
| 입력 | 0x7004 | tst_count | U32, pulse | 1~4,294,967,295 | 목표 펄스 수(축당 같은 값, 축별은 0x7040~) |
| 입력 | 0x7006 | tst_accel_ms / 0x7007 tst_decel_ms | U16 | 10~60,000 | 기존 정의(시작 → 설정 속도)[E2] |
| 입력 | 0x7008 | tst_repeat | U32 | 1~10^6 | RUN_SEQ 반복 수 |
| 입력 | 0x700A | tst_seed | U32 | | 무작위 이동 시드(T-04) |
| 입력 | 0x700C | load_nvs_period_ms | U16 | 0(끔), 10~10,000 | NVS 쓰기 부하 주기(T-06) |
| 입력 | 0x700D | load_cpu_pct | U16 | 0~90 | 저우선 태스크 CPU 부하 [가정 구현: 바쁜 루프 듀티] |
| 입력 | 0x700E | cfg_upd_period_us | U16 | 500~10,000 | 속도 갱신 주기(재시작 시 반영) |
| 입력 | 0x7010 | cfg_res_hz | U32 | 2 M/10 M/20 M/40 M [가정] | MCPWM 해상도(재시작 시) |
| 입력 | 0x7012 | cfg_flags | BITS16 | b0 PCNT IRAM 안전(빌드 옵션 표시만), b1 코어 배치안 A/B, b2 고장 브레이크 사용, b3 ETM 정지 실험 | 일부는 빌드 설정(읽기 전용 표시) |
| 출력 | 0x7020 | seq_done | U32 | | 완료한 이동 수 |
| 출력 | 0x7022 | err_count_ax1 / 0x7024 err_count_ax2 | U32 | | PCNT ≠ 목표 건수 |
| 출력 | 0x7026 | overshoot_ax1 / 0x7028 overshoot_ax2 | U32 | | 목표 초과 펄스 합 |
| 출력 | 0x702A | upd_late_count | U32 | | 갱신 지연 건수 |
| 출력 | 0x702C | upd_max_us | U32 | | 갱신 콜백 최대 처리 시간 |
| 출력 | 0x702E | isr_lat_max_ns_ax1 / 0x7030 _ax2 | U32 | | 감시점 ISR 최대 지연 |
| 출력 | 0x7032 | nvs_writes / 0x7034 nvs_max_ms | U32 | | NVS 쓰기 수·최대 소요 |
| 출력 | 0x7036 | chip_rev | U16 | | 부팅 시 읽은 칩 리비전(R-20) |
| 출력 | 0x7037 | cpu_mhz | U16 | | |
| 출력 | 0x7040~ | 축별 마지막 목표·PCNT 값 | U32 | | 축1 0x7040·0x7042, 축2 0x7044·0x7046 |
| 오류 | 0x7038 | tst_status | U16 | 0 대기, 1 실행, 2 완료, 0x10 거부(운전 중), 0x11 값 범위, 0x12 자원 없음(축2 init 실패) | |

공개 API(시그니처 수준):
```c
void     app_main(void);
uint16_t tfw_handle_cmd(uint16_t cmd);          /* called from TFW-03 register write */
void     tfw_get_results(tfw_results_t *out);   /* snapshot for TFW-03 reads */
```

동작(의사코드):
```
app_main: init_nvs(); log chip rev/clock/config; tfw_pulse2_init(axes, res_hz, upd_us);
          tfw_estop_init(GPIO19, polarity cfg); tfw_modbus_start(192.168.0.220, 502, unit 1)
          start load task (prio lowest) ; start sequencer task (prio below pulse update, above COMM)
RUN_SEQ: for i in repeat: for axis in mask: count = (seed ? rand_dist() : tst_count)
                          tfw_pulse2_move(axis, freq, count, accel, decel)   /* both axes started back-to-back */
         wait all done ; compare PCNT vs count -> err/overshoot counters ; seq_done++
load task: every load_nvs_period_ms: nvs_set_blob(32 B) + nvs_commit ; record duration
```

## 4. 제약 조건
- 시퀀서 태스크는 L1(TFW-02 ISR·갱신 콜백)을 막지 않는다. 결과 카운터 갱신은 잠금 없는 단일 작성자 구조.
- 시험 창 주소 0x7000~는 제품 맵(interface-spec §5.2) 예약 영역 밖이다. 제품 펌웨어에는 포함하지 않는다.
- 빌드 설정 2종(`sdkconfig.tfw_iram_on`, `sdkconfig.tfw_iram_off`)으로 T-06 비교 [가정].
- 기본 IP·포트·유닛은 기존과 같게(192.168.0.220:502, 1)[E5] 해 기존 HMI를 함께 접속할 수 있게 한다.

## 5. 완료 기준 (각 항목은 단위 테스트로 검증 가능해야 함)
| # | 기준 | 검증 방법 |
|---|---|---|
| 1 | 부팅 로그에 칩 리비전·CPU 클록·해상도·갱신 주기·핀 배정 출력 | HIL(시리얼 로그) |
| 2 | 시험 창 전 항목 읽기/쓰기, 범위 밖 값 → tst_status 0x11 | HIL(TFW-05) |
| 3 | RUN_SEQ 2축 1,000회 실행 후 seq_done = 1,000, 카운터 일관 | HIL |
| 4 | NVS 부하 on/off 전환, nvs_writes 증가 | HIL |
| 5 | 축2 자원 할당 실패 시 0x12와 로그(그룹 1·PCNT 유닛 2 확인 근거) | HIL |

## 6. 미확인·가정
- CPU 부하 생성 방식, 코어 배치안 A/B 정의([가정] A: 시퀀서·COMM 코어 0 / 갱신 코어 1, B: 반대)는 ④ coder가 정해 보고서에 적는다.
- NVS 쓰기 크기 32 B·주기 [가정].
- 축2 핀은 TFW-02 빌드 설정값(PS-27 결과 전 사용자 배선에 맞춤).
