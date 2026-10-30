# 작업 기록: 시험 펌웨어 TFW-01~05 (D-025 예외, G3 전 실측용)

| 항목 | 내용 |
|---|---|
| 대상 | TFW-01 tfw_main, TFW-02 tfw_pulse2, TFW-03 tfw_modbus, TFW-04 tfw_estop_io, TFW-05 tfw_host (설계서 v0.1) |
| 작성 | coder, 2026-10-30 |
| 근거 | decisions.md D-024~D-028, modules/TFW-01~05, interface-spec §2.4·§5.0.1, architecture §6 |
| 범위 | 시험 펌웨어만. 제품 모듈(MOD-xx) 구현 없음 |

## 0. 상태 요약 (먼저 읽을 것)

- **호스트에서 실행·검증한 것**: 하드웨어와 무관한 로직 전부(프로파일, 주파수 분해능, 레지스터 창 인코딩·범위검사, 시퀀서, 판정, 비상정지 래치 규칙, Modbus PDU 엔진, TFW-05 Python 도구).
- **미검증(사용자 PC에서 실행 필요)**: ESP-IDF 빌드, 보드 위 동작 전부(MCPWM/PCNT/GPTimer/EMAC/GPIO 인터럽트 코드, 부팅 로그, 실측). 이 컨테이너에는 ESP-IDF와 보드가 없고, 설치하지 않았다.
- 펌웨어 C 파일(tfw_pulse2.c, tfw_estop_io.c, tfw_modbus.c, tfw_main.c)은 **ESP-IDF 헤더 없이 스텁으로 문법만 검사**(`gcc -fsyntax-only`, 오류 0, 스텁 한계로 인한 무관한 경고만)했다. ESP-IDF API 이름·시그니처는 기억과 기존 코드(servo1axis)에 근거했고 **실제 헤더와 대조하지 못했다**. 빌드 시 컴파일 오류가 나면 해당 API 부분부터 확인한다(§6 위험 목록).

## 1. 구현 요약

산출물 위치(모두 `project/04_code/` 아래):

| 모듈 | 파일 | 내용 |
|---|---|---|
| 공통 로직(호스트 테스트 가능) | `test-fw/components/tfw_logic/` (`tfw_core.c`, `tfw_regwin.c`, `tfw_seq.c`, `tfw_mb.c`, `include/tfw_logic.h`, `include/tfw_regs.h`) | 주파수→틱 변환, 프로파일, 갱신 통계, 판정, 난수, 시험 레지스터 창, 시퀀서(하드웨어는 함수표 뒤), 비상정지 래치 규칙, Modbus PDU 엔진(레거시 창+시험 창), 히스토그램 구간 |
| TFW-01 | `test-fw/main/tfw_main.c`, `tfw_main.h`, `Kconfig.projbuild` | 앱 진입, 부팅 로그, 코어 배치 A/B, 명령 처리 `tfw_handle_cmd`, 결과 스냅샷 `tfw_get_results`, 시퀀서 태스크, NVS·CPU 부하 태스크, cfg 레지스터 NVS 저장 |
| TFW-02 | `test-fw/components/tfw_pulse2/` | 2축 MCPWM(그룹=축 번호)+PCNT, GPTimer(HWTIMER)·10 ms 태스크 갱신 방식, 자원 탐색 `probe_caps`, 통계 |
| TFW-03 | `test-fw/components/tfw_modbus/` | IP101 Modbus TCP 서버(W5500 제거), 접속 수 제한, 처리 시간 히스토그램, 링크 다운 카운트, 레거시 축1 운전 태스크 |
| TFW-04 | `test-fw/components/tfw_estop_io/` | GPIO19 엣지 ISR(경로 A), MCPWM 고장 입력 브레이크(경로 B), 래치·해제, 트립 후 펄스 수 측정 |
| TFW-05 | `test-fw/host/tfw_host/` (`regs.py`, `modbus.py`, `load.py`, `la.py`, `judge.py`, `scenario.py`, `report.py`, `cli.py`, `mock.py`), `test-fw/host/scenario_example.yaml` | 부하 생성, 로직 분석기 CSV 평가, 판정, 시나리오 실행, 14개 T-ID 보고서 |
| 빌드 설정 | `test-fw/CMakeLists.txt`, `sdkconfig.defaults`, `sdkconfig.tfw_iram_on`, `sdkconfig.tfw_iram_off`, `tools/check_iram.sh` | IRAM 안전 on/off 두 설정, ISR 경로 점검 스크립트(미실행) |
| 단위 테스트 | `tests/test-fw/` (`test_logic.c`, `Makefile`, `bridge.c`, `test_*.py`, `conftest.py`) | 아래 §3 |

`.gitignore`를 `project/04_code/`에 추가했다(빌드 산출물·`results/`·`sdkconfig` 제외).

### 기존 코드 사용 출처 (servo1axis, 커밋 2135568, 읽기 전용으로 읽고 복사·수정)

| 가져온 것 | 출처 | 시험 펌웨어 위치 | 수정 |
|---|---|---|---|
| MCPWM 타이머·오퍼레이터·비교기·제너레이터 설정, PCNT 30,000 블록 누적, 감시점 추가·제거, 시작 순서 | `components/motion_core/motion_core.c` [E1] | `tfw_pulse2.c` | 정적 변수 → 축 배열, 그룹 = 축 번호, 해상도 가변, `update_period_on_empty`, 위험 3건 대응(§2) |
| `calculate_profile`, `profile_frequency` | `components/movion_modbus/movion_modbus_tcp.c` [E2] | `tfw_core.c`(`tfw_profile_calc`), `tfw_logic.h`(`tfw_profile_freq`) | 인스턴스별 상태, ISR용 float32 식(§2-3) |
| `process_request`(FC01/02/03/05/06/15/16, 예외 코드, 레거시 규칙) | 같은 파일 [E2] | `tfw_mb.c` | NVS 호출 제거, 시험 창 분기 추가, 레거시 유효성 규칙 동일 |
| `receive_exact`/`send_exact`, `client_task`, `server_task`, IP101 EMAC 초기화(PHY 주소 1, 리셋 GPIO51, 고정 IP, DHCP 끔), 이벤트 핸들러, 서보 출력 GPIO23/26/27/46 | 같은 파일 [E2] | `tfw_modbus.c` | W5500 경로 제거(D-027), 접속 수 제한·계측 추가 |
| `motion_task` 축소판(START·FORCE STOP·완료 판정) | 같은 파일 [E2] | `tfw_modbus.c`의 `legacy_motion_task` | 축1 전용, 프로파일 갱신은 TFW-02가 담당 |
| `init_nvs` | `main/app_main.c` [E4] | `tfw_main.c` | 없음 |
| flash/console 설정 | `sdkconfig.defaults` | `test-fw/sdkconfig.defaults` | W5500 줄 제거, `FREERTOS_HZ=1000` 추가 |
| 기본 IP 192.168.0.220:502, 유닛 1 | `main/movion_network_config.h` [E5] | `main/Kconfig.projbuild` | 빌드 설정화 |

래더 코드(`movion_ladder*`), RTU, W5500는 가져오지 않았다. servo1axis 디렉터리는 수정하지 않았다.

## 2. architect 지적 기존 코드 위험 3건의 처리

1. **2 MHz 해상도에서 요청보다 높은 주파수 출력 가능성** (기존 `motion_set_frequency`는 `period = res / f` 내림)
   - 기존 규칙의 문제를 테스트로 재현했다: 2 MHz, 요청 300 kHz → 내림 6틱 = 333,333 Hz로 요청 초과(`t_old_floor_rule_would_exceed`).
   - 처리: `tfw_ticks_for_freq()`가 `ticks = ceil(res/f)`, 최소 2를 쓴다. 이때 출력 주파수 `res/ticks`는 요청을 넘지 않는다(2/10/20/40 MHz × 153 Hz~1 MHz 전수 검사, `t_ticks_never_exceed_request`).
   - 주기가 `max_period_ticks`(기본 65,535 [가정], `probe_caps`가 낮추기만 함)를 넘는 저주파 요청은 **거부(ESP_ERR_INVALID_ARG)**한다. 최대값으로 자르면 요청보다 높은 주파수가 나가기 때문이다. 결과로 최저 주파수는 `res / 65535`(10 MHz에서 153 Hz, 40 MHz에서 611 Hz)다. 시작 속도 `min(5000, 속도)`는 모든 해상도에서 통과하지만, 속도 100~610 Hz 이동은 40 MHz에서 거부된다. T-09에서 확인할 값이다.
   - 비교기 값은 `ticks/2`(≥1), 타이머 플래그 `update_period_on_empty`와 비교기 `update_cmp_on_tez`로 주기·비교값이 같은 TEZ에서 바뀐다.
2. **PCNT ISR의 IRAM 안전성**
   - 감시점 콜백 `on_reach`, 갱신 `update_all`, GPTimer 콜백, 비상정지 ISR, `tfw_pulse2_stop`, `tfw_pulse2_count_isr`에 `IRAM_ATTR`. ISR에서 쓰는 변수는 DRAM 정적 변수.
   - ISR에서 RTOS API를 호출하지 않는다. 완료는 `volatile bool done` 플래그이고 시퀀서가 1 ms마다 확인한다(기존은 `xEventGroupSetBitsFromISR`).
   - ISR 안의 계산은 정수·단정도 float만 쓴다. 기존 `calculate_profile`/`profile_frequency`의 double `sqrt`는 플래시의 libm·soft-double을 부를 수 있어, ISR용 식을 float32(`__builtin_sqrtf`)로 바꿨다(P4는 단정도 FPU가 있다 [가정]). 64비트 나눗셈과 libm 호출 없음.
   - 드라이버 쪽은 `sdkconfig.tfw_iram_on`(`PCNT_ISR_IRAM_SAFE`, `PCNT_CTRL_FUNC_IN_IRAM`, `MCPWM_ISR_IRAM_SAFE`, `MCPWM_CTRL_FUNC_IN_IRAM`, `GPTIMER_*`, `GPIO_CTRL_FUNC_IN_IRAM`)와 기존 동작을 재현하는 `sdkconfig.tfw_iram_off` 두 벌로 T-06을 비교한다. 기존 sdkconfig에서는 `MCPWM_ISR_HANDLER_IN_IRAM`만 켜져 있고 나머지 IRAM 안전 옵션은 모두 꺼져 있음을 확인했다. `cfg_flags` b0(0x7012)는 빌드 설정을 표시한다(읽기 전용).
   - 옵션 이름은 ESP-IDF 5.5 기준 기억이다. **빌드 후 생성된 sdkconfig에서 실제로 켜졌는지 확인해야 한다**(§4 절차 3). `tools/check_iram.sh`는 ISR 경로 함수가 IRAM 주소에 있는지 점검한다(미실행).
   - 갱신 콜백이 부르는 `mcpwm_timer_set_period`, `mcpwm_comparator_set_compare_value`, `pcnt_unit_get_count`가 ISR에서 호출 가능한지는 **실제 헤더 문서로 확인하지 못했다**. HWTIMER 방식이 실패하면 `CONFIG_TFW_UPD_MODE_HWTIMER=n`(10 ms 태스크)으로 바꿔 비교한다.
3. **Modbus 쓰기마다 NVS 저장**
   - PDU 엔진(`tfw_mb.c`)은 저장소를 전혀 호출하지 않고 레거시 값이 바뀌면 `dirty` 플래그만 세운다(`t_mb_no_persist_side_effect`). 기본 동작은 저장 없음이다.
   - `CONFIG_TFW_LEGACY_NVS_ON_WRITE=y`일 때만 기존처럼 쓰기마다 저장해 T-06 비교에 쓸 수 있다.
   - NVS는 (a) 부하 태스크(시험 의도), (b) cfg 레지스터 3종(0x700E/0x7010/0x7012)을 바꿀 때 1회, 두 경우에만 쓴다. cfg 저장은 운전 중에는 거부된다.

STOP/비상정지 입력은 D-027에 따라 **GPIO19**(`CONFIG_TFW_ESTOP_GPIO` 기본값). 부팅 시 레벨을 읽어 로그에 남기고 이미 활성이면 경고한다("기존 코드는 GPIO18" 문구 포함).

## 3. 테스트와 결과 (호스트, 실제 실행 결과 그대로)

실행한 명령과 결과(2026-10-30, gcc 13.3.0, Python 3.13.17, pytest는 `pip install pytest`로 설치):

```
$ cd project/04_code/tests/test-fw && make run
  (gcc -std=c11 -Wall -Wextra -O1 ... -lm, 경고 없음)
  31개 테스트 함수 전부 PASS, checks=16553 failed=0

$ cd project/04_code/tests/test-fw && python3 -m pytest -q
  30 passed in 20.95s
```

- 합계: C 31개 테스트 함수(검사 16,553건) 통과 0 실패, pytest 30개 통과 0 실패.
- **기록할 실패 이력**: 최초 실행에서 C 검사 4건과 pytest 3건이 실패했다. 모두 코드 결함이 아니라 테스트 기대값 또는 모의 장치의 오류였고 원인을 확인한 뒤 수정했다. 테스트를 삭제하거나 검사를 약화해 통과시키지 않았다.
  - `ceil(10 MHz/153)`을 65,359로 잘못 기대(올바른 값 65,360), 끝점 주파수 기대값 오류(남은 1펄스 구간의 식 값으로 교체), 삼각형 프로파일에서 최대값이 해석적 정점보다 최대 약 0.1% 높음(기존 코드의 가속 펄스 수 내림 때문이며 목표 속도 클램프로 안전. 허용 오차 0.5%로 명시), 응답 바이트 오프셋 계산 오류, 히스토그램 기대값 합산 오류(4건).
  - 모의 서버의 `CLEAR_STATS`가 카운터를 지우지 않았음, LA 합성 CSV의 시간 해상도(ns)가 285,714 Hz 검사에 부족함(1건씩).
  - 최초 C 실행 중 테스트 코드의 버퍼 크기 오류로 스택 파손이 한 번 났다(테스트 코드의 해당 줄 삭제 — 대형 읽기는 `t_mb_window_125_registers_and_writes`가 따로 검증).

### 완료 기준별 검증 현황

| 모듈 | # | 기준 | 호스트에서 검증한 부분 | 남은 것 |
|---|---|---|---|---|
| TFW-01 | 1 | 부팅 로그 | 없음(로그 문구는 코드에 있음) | HIL |
| TFW-01 | 2 | 시험 창 전 항목 읽기/쓰기, 범위 밖 → 0x11 | `t_regwin_all_inputs_roundtrip`, `t_regwin_out_of_range_sets_0x11`(21개 경계), `t_regwin_outputs_map`, `t_regwin_exceptions`, `test_c_bridge.py`(Python 클라이언트 ↔ 실제 C PDU 엔진, 125개 읽기 포함) | HIL(TFW-05) |
| TFW-01 | 3 | RUN_SEQ 2축 1,000회 → seq_done 1,000, 카운터 일관 | `t_seq_1000_iterations_two_axes`, `t_seq_counters_consistent_with_injected_faults`(주입 오류 10건/초과 20펄스가 카운터와 일치), 난수 재현성, 중단·시작 실패 | 모의 축으로만 검증. 실제 2축은 HIL |
| TFW-01 | 4 | NVS 부하 on/off | 레지스터 범위·효과 플래그(`load_changed`) | 실제 `nvs_writes` 증가는 HIL |
| TFW-01 | 5 | 축2 실패 시 0x12 | `t_cmd_check`(축2 불가 → 0x12, 축1은 정상) | 실제 자원 실패 로그는 HIL |
| TFW-02 | 1,7 | 축2 생성, probe_caps | 없음 | HIL 부팅 로그 |
| TFW-02 | 2,3,4,6 | 주파수, 펄스 수, 갱신 비교, ISR 지연 | 프로파일·갱신 통계·판정 로직 | HIL |
| TFW-02 | 5 | 해상도별 freq_actual ≤ 요청 | 4개 해상도 전수 검사, 거부 규칙 | 경계 펄스 폭·실제 출력은 HIL |
| TFW-03 | 1~4 | HMI, 4클라이언트, 케이블 분리, 5번째 거부 | PDU 엔진(레거시 창 동작·예외 코드), 히스토그램 구간, 모의 서버의 4접속 제한(Python 쪽) | 펌웨어 쪽 접속 제한·링크 복구·HMI는 HIL |
| TFW-04 | 1,3,4,5 | 경로별 시간, bind 결과, 입력 지연, 트립 후 펄스 | 래치·해제 규칙, 트립 후 펄스 계산 | HIL |
| TFW-04 | 2 | 래치 중 명령 거부, 입력 활성 중 해제 거부, 자동 재기동 없음 | `t_estop_latch_rules`(10 ms 안정 조건 포함), `t_estop_blocks_run_commands` | 실제 GPIO·재기동 없음은 HIL |
| TFW-05 | 1 | count_edges/freq_stats 정확 | `test_la.py`(1~1,000,000 펄스, 100 Hz~1 MHz, 지터, 채널 오류) | 통과 |
| TFW-05 | 2 | load_clients 4연결 | `test_load.py`(로컬 모의 서버, FC03 125개, FC03/16 혼합, 분포·히스토그램) | 통과 |
| TFW-05 | 3 | T-03~T-09 실행·JSON | 모의 장치(`mock.py`)에 대한 실행만 | HIL(모의 장치는 펌웨어가 아님) |
| TFW-05 | 4 | 보고서에 14개 T-ID | `test_report_lists_all_14_ids_with_substitutions`, 빈 결과에서도 14행 | 통과 |

## 4. 사용자 PC에서 실행할 절차 (미검증)

전제: ESP-IDF 5.5.3 설치, Waveshare ESP32-P4-ETH 보드, 로직 분석기(4채널 이상, 최소 10 MS/s [가정]), 스위칭 허브. 기존 IP101 배선(192.168.0.220)과 기존 HMI 연결을 그대로 쓴다.

1. **준비**: `cd project/04_code/test-fw && idf.py set-target esp32p4`. `idf.py menuconfig` → "TFW test firmware"에서 **축2 핀(`TFW_AX2_*`, 기본값 2/3/4는 근거 없는 임시값), 비상정지 극성, 디버그 GPIO, 최대 접속 수**를 실제 배선에 맞춘다. 축2 핀이 보드 내부 사용 핀과 겹치면 안 된다(PS-27 미확인).
2. **빌드(IRAM on/off 두 벌)**:
   - `idf.py -B build_on -D SDKCONFIG=sdkconfig.on -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.tfw_iram_on" build`
   - `idf.py -B build_off -D SDKCONFIG=sdkconfig.off -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.tfw_iram_off" build`
   - 컴파일 오류가 나면 §6 위험 목록의 API부터 확인하고 결과를 coder에게 알린다.
3. **옵션 확인**: `grep -E "PCNT_ISR_IRAM_SAFE|MCPWM_ISR_IRAM_SAFE|GPTIMER_ISR_IRAM_SAFE|CTRL_FUNC_IN_IRAM" sdkconfig.on sdkconfig.off`로 on 쪽이 `=y`인지 확인(이름이 틀리면 조용히 무시될 수 있다). `tools/check_iram.sh build_on/tfw_p4_pretest.elf`로 ISR 경로 점검.
4. **플래시·모니터** (사용자 실행): `idf.py -B build_on -p <PORT> flash monitor`. 부팅 로그에서 칩 리비전, CPU 클록, 해상도·갱신 주기, IRAM 옵션, 핀 배정, `CAPS ...`(그룹 1, 제너레이터 2, PCNT 채널 2, max_period_ticks), `bind_ok`, GPIO19 부팅 레벨을 기록한다. 축2 실패 시 `AXIS 2 UNAVAILABLE`과 오류 코드가 나온다.
5. **PC 도구**: `cd test-fw/host && python3 -m tfw_host.cli run scenario_example.yaml --target 192.168.0.220:502 --results results` 후 `python3 -m tfw_host.cli report results reports/report_G3_pretest.md`. `results/meta.json`에 빌드 해시·sdkconfig 차이·장비를 직접 적는다. 로직 분석기 채널 이름은 CSV 헤더와 시나리오의 `la_pulse_channels`/`la_trigger_channel`을 맞춘다.
6. **T-05**: `TFW_MAX_CLIENTS=8`로 따로 빌드(제어 연결 + 부하 4 + HMI). 5번째 접속 거부 확인(TFW-03 #4)은 기본값 4 빌드로 한다.
7. **T-07**: GPIO19를 수동 스위치 또는 별도 디지털 출력으로 구동하고 `es_path_sel`(0x7060)로 경로를 바꾼다. 해제는 0x7061에 1.

## 5. 설계와 다른 점

| # | 설계서 | 구현 | 사유 |
|---|---|---|---|
| 1 | TFW-05: Modbus 라이브러리 [가정: pymodbus] | 자체 최소 Modbus TCP 클라이언트(`modbus.py`) | 컨테이너에 pymodbus 없음, 외부 의존 없이 시험 가능. 필요하면 어댑터로 교체 |
| 2 | TFW-05: 시나리오 YAML [가정] | YAML 또는 JSON 둘 다 읽음 | PyYAML 없는 PC 대응 |
| 3 | TFW-02: 감시점 ISR 지연 측정 "ISR 내부 계측은 사이클 카운터 읽기 2회 이내" | 내부 값은 **추정**: ISR 진입 시 PCNT 값 − 감시점 값(= 지연 중 나간 펄스 수) × 주기. 분해능 1펄스(1 MHz에서 1 µs). 사이클 카운터 방식은 쓰지 않음 | 감시점 도달 시각을 알 수 없어서 사이클 카운터만으로는 지연이 안 나옴. 정밀 값은 디버그 GPIO(`TFW_DEBUG_GPIO`) + 로직 분석기 `latency()` |
| 4 | TFW-04: `es_isr_lat_ns`(엣지 → ISR 진입) | 펌웨어 내부 측정 **없음**, 값 0xFFFFFFFF(미측정). 디버그 GPIO(`TFW_DEBUG_GPIO_ESTOP`) 토글 + 외부 측정 | 엣지 시각의 하드웨어 타임스탬프(MCPWM 캡처 등)를 구현하지 않음. T-12는 외부 측정에 의존 |
| 5 | TFW-02: ETM 선택 실험 | 구현하지 않음. `probe_caps`가 `etm_tested=false`, `NOT_SUPPORTED`로 보고 | 설계서가 선택 실험이며 결과가 없어도 완료로 봄. API 존재 미확인 |
| 6 | TFW-04: `tfw_estop_init(gpio, active_low, oper[2])`로 제너레이터 접근 | `tfw_pulse2_generator(axis)` 접근자를 TFW-02에 추가 | 브레이크 동작을 제너레이터에 설정해야 함 |
| 7 | TFW-01 공개 API만 | TFW-02에 `set_update_core`, `axis_ok`, `set_dir`, `count_isr`, `clear_stats`, `operator/generator` 접근자, TFW-03에 `set_hooks`, `set_core`, `clear_stats` 추가 | 모듈 간 결합(코어 배치, 콜백 등록). TFW-03이 TFW-01을 직접 참조하지 않도록 훅 사용 |
| 8 | TFW-04: `es_fault_bind_ok` 0/1/3 | 그룹 1만 성공이면 2 | 값이 하나 더 필요 |
| 9 | TFW-02: 시험 창 `0x7040~` "축별 마지막 목표·PCNT 값"과 TFW-01 `tst_count`의 "축별은 0x7040~" 표현이 충돌 | 0x7040~0x7047을 **출력 전용**으로 구현, 축별 목표 입력은 없음 | 설계서 모순. 축별 목표가 필요하면 설계 확정 후 주소 배정 |
| 10 | TFW-01 `tst_status` | 운전 중(1)에 범위 밖 값을 쓰면 0x11을 덮어쓰지 않고 값만 무시하며 상태 1 유지. 운전 중 cfg 레지스터 쓰기도 무시 | 단일 필드에 실행 상태와 오류 코드가 겹쳐서 실행 상태가 사라지는 것을 막음. 반면 운전 중 명령 거부(0x10)는 설계대로 상태를 0x10으로 바꾼다 |
| 11 | TFW-03: 최대 접속 4 [가정] | 기본 4, Kconfig로 조정 | T-05(부하 4 + HMI + 제어 연결)와 충돌, §7 이슈 |
| 12 | TFW-01 `cfg_*` "재시작 시 반영" | 쓰면 NVS에 1회 저장하고 다음 부팅에서 적용. 재부팅은 사용자가 수동으로 한다 | 재시작 명령이 설계에 없음 |
| 13 | 설계 "시험 보고서 위치 `project/04_code/test-fw/reports/`" | 폴더만 만들고 보고서 생성은 사용자 실행 시 | 실측 전 |

## 6. 문법 검사로 확인하지 못한 ESP-IDF API (빌드 시 먼저 볼 곳)

`mcpwm_timer_config_t.flags.update_period_on_empty` 필드명, `mcpwm_gpio_fault_config_t`(`flags.active_level/pull_up/pull_down`), `mcpwm_brake_config_t`, `MCPWM_GEN_BRAKE_EVENT_ACTION` 매크로, `mcpwm_operator_recover_from_fault`, `mcpwm_new_generator`에 `gen_gpio_num = -1`(probe의 제너레이터 2 시험), `pcnt_new_channel`에 `edge_gpio_num = -1`(probe의 PCNT 채널 2 시험), `esp_clk_tree_src_get_freq_hz`의 인자, `esp_chip_info_t.revision`의 값 의미(예: 301 = v3.1), GPTimer 콜백에서 MCPWM/PCNT 제어 함수 호출 가능 여부. probe의 `-1` 사용이 실패해도 오류 코드(`err_gen2`, `err_pcnt_ch2`)가 로그에 남으므로, 그 코드가 핀 인자 때문인지 자원 부족 때문인지 구분해서 읽어야 한다.

## 7. 이슈 제기

1. **(설계 모순) T-05 접속 수**: T-05는 부하 클라이언트 4개 + 실제 HMI 1대 + 제어 연결(총 6)을 요구하지만 TFW-03은 최대 접속 4이고 5번째 거부가 완료 기준이다. 제품 `max_clients` 기본 4 [가정](interface-spec §5.3)의 의미(HMI 포함 여부)도 결정이 필요하다. 시험 펌웨어는 Kconfig로 우회했다.
2. **(설계 모호) `tst_count` 축별 입력 / 0x7040 영역 충돌**: §5의 9번.
3. **(설계 결함 후보) 감시점 ISR 지연 정의**: "목표에 도달한 시각"을 펌웨어가 알 수 없어 내부 계측은 추정값이다. f_stop_max 산정(T-08)에는 로직 분석기 측정(디버그 GPIO 토글 → 마지막 정상 펄스 사이)이 필요하다고 명시해야 한다.
4. **(제품 설계에 영향) 저주파 거부 규칙**: 요청 초과 출력을 막기 위해 `ticks > max_period_ticks`를 거부로 처리하므로 최저 속도가 해상도에 묶인다(40 MHz에서 약 611 Hz). NFR의 최저 속도 요구(속도 범위 100 Hz~)와 해상도 선택(MOD-02·07)에 반영해야 한다. 100 Hz 속도 요구가 있으면 해상도 ≤ 6.5 MHz이거나 타이머 폭이 더 넓어야 한다(`probe_caps`로 확인).
5. **(제품 설계에 영향) 갱신 콜백의 float32 식**: ISR 안전을 위해 float32를 썼다. MOD-07 제품 설계에서 ISR 내부 연산 형식(정수 룩업표 또는 float32)을 결정해야 한다. 정확도는 해석식 대비 상대오차 1e-4 이내를 호스트에서 확인했다.
6. **(확인 요청) 축2 핀**: 기본값 GPIO2/3/4는 근거 없는 임시값이다. 사용자 배선으로 지정해야 하며 PS-27 결과가 나오기 전에는 확정할 수 없다. 해당 핀이 W5500 후보 핀(설계 문구)이지만 이번 보드에서 다른 용도로 쓰이는지는 미확인.
7. **(확인 요청) 시험 레지스터 창에 `probe_caps` 결과 주소 없음**: TFW-02 §3은 "④에서 주소 배정"으로 남겨 두었다. 부팅 로그에만 있다.
8. 실행하지 못한 항목 전체는 §0과 §3의 "남은 것" 열이다. 빌드가 한 번도 되지 않았으므로 **빌드 성공 여부 자체가 미검증**이다.
