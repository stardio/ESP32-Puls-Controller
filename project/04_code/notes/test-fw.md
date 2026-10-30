# 작업 기록: 시험 펌웨어 TFW-01~05 (D-025 예외, G3 전 실측용)

| 항목 | 내용 |
|---|---|
| 대상 | TFW-01 tfw_main, TFW-02 tfw_pulse2, TFW-03 tfw_modbus, TFW-04 tfw_estop_io, TFW-05 tfw_host (설계서 v0.1) |
| 작성 | coder, 2026-10-30 |
| 근거 | decisions.md D-024~D-028, modules/TFW-01~05, interface-spec §2.4·§5.0.1, architecture §6 |
| 범위 | 시험 펌웨어만. 제품 모듈(MOD-xx) 구현 없음 |

## 0. 상태 요약 (먼저 읽을 것)

- **리뷰 2차 반영(반복 3회차, 마지막, 2026-10-30)**: §9에 지적별 반영·반박 표가 있다. 달라진 부분은 §9가 §8보다 우선한다. 결과는 §3 끝의 "3회차 결과".
- **리뷰 1차 반영(반복 2회차, 2026-10-30)**: 아래 §8에 지적별 반영·반박 표가 있다. §1~§7은 1회차 기록이며, 달라진 부분은 §8이 우선한다(테스트 수·결과는 §3 끝의 "2회차 결과", 축2 핀 기본값은 §8).

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

### 2회차 결과 (리뷰 1차 반영 후, 실제 실행 그대로)

```
$ cd project/04_code/tests/test-fw && make run
  (gcc -std=c11 -Wall -Wextra -O1 ... -lm, 경고 없음)
  38개 테스트 함수 전부 PASS, checks=16583 failed=0

$ cd project/04_code/tests/test-fw && python3 -m pytest -q
  212 passed in 30.68s
```

- 1회차 대비 C 7개 함수(검사 +30건), pytest +182건(`test_judge_regress.py` 161, `test_firmware_static.py` 12, `test_expect.py` 9).
- 도중 실패 이력(삭제·약화 없이 원인을 고침): (1) `wait_status=None`(타임아웃)을 "키 누락"으로 처리해 NOT_RUN이 나옴 -> 타임아웃은 FAIL이어야 하므로 판정 코드를 고침(테스트 6건이 잡음). (2) T-05 접속 거부 시험이 실패 -> 펌웨어는 접속을 받은 뒤 닫으므로 `connect()` 성공만 보면 거부가 안 잡힘. 부하 클라이언트가 첫 요청에서 연결이 닫히면 거부로 세도록 `load.py`를 고침(실제 결함, C-03 보강). (3) C 시험 `t_r1_seq_aborted...`가 "중단 시 오차 0"을 가정했으나 fake 기계에서는 중단된 반복이 오차 1로 센다 -> 가정이 틀렸으므로 검사 문구를 고침. (4) `test_firmware_static.py`가 주석 속 단어를 호출로 읽음 -> 주석 제거 후 검사.
- **회귀 시험이 실제로 결함을 잡는지 확인**: 1회차 코드(`git archive HEAD`)를 임시 폴더에 풀어 새 `test_judge_regress.py`/`test_firmware_static.py`를 돌리면 166건 실패, 7건 통과(대부분 판정 키 누락·미구현 항목). 새 판정 규칙에서는 거짓 합격 시나리오(거부·타임아웃·중단·외부 계수 없음·접속 거부·NVS 부하 0·iram 불일치)가 모두 PASS가 아니다.
- 기존 시험 변경: `test_run_scenario_t03_to_t09_with_mock`의 T-04 기대값을 PASS에서 NOT_RUN으로, `test_judge_rules`는 판정 입력 계약(실행 결과 키 필수)에 맞춰 입력을 완전한 실행 결과로 바꿨다. 이는 1회차 판정이 "반복 10회, 외부 계수 없음, 완료 확인 없음"에서 PASS를 낸 것이 결함이었기 때문이며 기준을 낮춘 것이 아니라 높인 것이다.

### 3회차 결과 (리뷰 2차 반영 후, 실제 실행 그대로, 2026-10-30)

```
$ cd project/04_code/tests/test-fw && make run
  (gcc -std=c11 -Wall -Wextra -O1 ... -lm, 경고 없음)
  41개 테스트 함수 전부 PASS, checks=16591 failed=0

$ cd project/04_code/tests/test-fw && python3 -m pytest -q
  262 passed in 42.26s
```

- 2회차 대비 C 3개 함수(`t_r2_seq_*`, 검사 +8건), pytest +50건(`test_round2_regress.py` 29, `test_cfg_syntax.py` 21).
- **회귀 시험이 2회차 코드에서 실패함을 확인**: 2회차 소스를 복사해 둔 임시 폴더에 새 시험을 덮어 실행(모의 서버 `mock.py`만 새 것으로 교체, 새 함수 이름 `judge_file`/`first_periods`는 import 오류를 피하려고 빈 껍데기를 붙임). 결과: pytest 새 시험 파일 2개 48건(n-3·n-8 시험 2건을 추가하기 전 시점) 중 **44건 실패**, 4건 통과(통과한 4건은 시험 도구 자체 검사 `test_stub_follows_the_bool_rule`, `test_n01_stub_check_detects_the_round2_defect`, 그리고 iram_on/legacy 조합의 `tfw_estop_io.c`로, 이 조합에서는 해당 매크로가 정의되어 2회차 코드도 컴파일되므로 리뷰 N-01 설명과 일치). C 시험은 2회차 `tfw_seq.c`로 빌드하면 새 함수 3개가 모두 실패(`failed=5`). 기존 시험 중 2회차 코드에서 13건이 실패하는 것은 판정 입력 계약(`la_fresh` 등)을 바꾼 시험을 2회차 코드에 돌렸기 때문이다.
- 기존 시험 변경(기준을 낮춘 것이 아니라 새 계약에 맞춘 것): 판정 입력의 `base()`에 `la_fresh`·`mb_clients_min`·`dropped`/`idle_clients` 추가, e2e 시험은 CSV를 **운전 전에** 만들던 순서를 운전 **후** 내보내는 순서(`capture` 훅)로 변경, T-07 시험에 `es_last_err`·`es_trip_count` 전제 추가, `test_m04...`가 고정 문자열로 확인하던 `tfw_estop_blocking` 반환식을 `!s_init_ok || ...`로 갱신.
- 도중 실패 이력: (1) 새 e2e 시험에서 무작위 seed(T-04, seed != 0)를 쓰면 기대 엣지 수가 2,000이 아니어서 FAIL -> 시험 입력 오류, 시험 시나리오에서 seed를 뺌. (2) 부하 단절 시험에서 가짜 서버가 MBAP 길이 필드를 4바이트로 잘못 보냄(시험 오류). (3) 정적 시험이 주석 속 `pcnt_unit_clear_count` 단어를 코드로 읽음 -> 호출 형태(`(a->pcnt)`)로 좁힘. 코드 결함으로 이어진 실패는 없었다.

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

1. **준비**: `cd project/04_code/test-fw && idf.py set-target esp32p4`. `idf.py menuconfig` → "TFW test firmware"에서 **축2 핀(`TFW_AX2_*`, 2회차부터 기본값 -1 = 축2 생성 안 함, 반드시 지정), 비상정지 극성, 디버그 GPIO, 최대 접속 수, probe 핀(기본 -1 = probe 생략)**을 실제 배선에 맞춘다. 축2 핀이 보드 내부 사용 핀(EMAC/RMII 등, 미확인 PS-27)과 겹치면 안 된다. 부팅 시 코드가 축1·축2·비상정지·디버그·probe·기존 코드 사용 핀(PHY 리셋 51, 서보 출력 23/26/27/46)과의 중복만 검사한다.
2. **빌드(IRAM on/off 두 벌)**:
   - `idf.py -B build_on -D SDKCONFIG=sdkconfig.on -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.tfw_iram_on" build`
   - `idf.py -B build_off -D SDKCONFIG=sdkconfig.off -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.tfw_iram_off" build`
   - 컴파일 오류가 나면 §6 위험 목록의 API부터 확인하고 결과를 coder에게 알린다.
3. **옵션 확인**: `grep -E "PCNT_ISR_IRAM_SAFE|MCPWM_ISR_IRAM_SAFE|GPTIMER_ISR_IRAM_SAFE|CTRL_FUNC_IN_IRAM" sdkconfig.on sdkconfig.off`로 on 쪽이 `=y`인지 확인(이름이 틀리면 조용히 무시될 수 있다). `tools/check_iram.sh build_on/tfw_p4_pretest.elf`로 ISR 경로 점검.
4. **플래시·모니터** (사용자 실행): `idf.py -B build_on -p <PORT> flash monitor`. 부팅 로그에서 칩 리비전, CPU 클록, 해상도·갱신 주기, IRAM 옵션, 핀 배정, `CAPS ...`(그룹 1, 제너레이터 2, PCNT 채널 2, max_period_ticks), `bind_ok`, GPIO19 부팅 레벨을 기록한다. 축2 실패 시 `AXIS 2 UNAVAILABLE`과 오류 코드가 나온다.
5. **PC 도구**: `cd test-fw/host && python3 -m tfw_host.cli run scenario_example.yaml --target 192.168.0.220:502 --results results` 후 `python3 -m tfw_host.cli report results reports/report_G3_pretest.md`. `results/meta.json`에 빌드 해시·sdkconfig 차이·장비를 직접 적는다. 로직 분석기 채널 이름은 CSV 헤더와 시나리오의 `la_pulse_channels`/`la_trigger_channel`을 맞춘다.
6. **T-05**: `TFW_MAX_CLIENTS=8`로 따로 빌드(제어 연결 + 부하 4 + HMI). 5번째 접속 거부 확인(TFW-03 #4)은 기본값 4 빌드로 한다.
6a. **M-05 (정지 후 첫 주기)**: 시나리오의 T-09 세 단계(1 MHz -> 5 kHz -> 1 MHz)는 **단계마다 별도 캡처 파일**을 쓴다. 캡처를 시작한 뒤 `run`(또는 `--wait-capture`)을 실행하고, 운전이 끝나면 캡처를 멈춰 내보낸다. `run`은 LA CSV가 필요한 단계를 PENDING으로 남기므로 `python3 -m tfw_host.cli judge results/<T-ID>_<시각>_<n>.json <캡처.csv>`로 판정한다(CSV 수정 시각이 운전 종료 이후이고, 길이가 운전 시간 - 0.5 s [가정] 이상이며, 같은 파일을 다른 실행에 쓰지 않아야 한다). 5 kHz 단계는 첫 3주기가 200 us ± LA 2샘플이어야 하고, 1 MHz로 돌아오는 단계는 첫 3주기가 1 us ± 2샘플이어야 한다(긴 쪽·짧은 쪽 모두 FAIL).
6b. **M-02 확인 (리뷰 2차 §3-1 조건 2·3)**: HWTIMER 모드 첫 이동 전에 (a) IDF 5.5 문서 "FreeRTOS (IDF) - Floating Point Usage"에서 ESP32-P4 항목(ISR 안 FPU 허용 여부)을 확인해 결과를 보고서에 적거나, (b) HWTIMER 운전 중 별도 태스크에서 float 검사합을 계속 계산해 불일치 0을 확인한다(태스크 FPU 레지스터 손상은 패닉 없이 조용히 틀리므로 "1회 이동에 패닉 없음"만으로는 부족하다). 불일치가 나오거나 (a)가 금지이면 `CONFIG_TFW_UPD_MODE_HWTIMER=n`(TASK 모드)로 비교한다. 이제 이 TASK 모드 빌드도 컴파일된다(N-01 수정).
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

## 8. 리뷰 1차(`reviews/test-fw-review-1.md`) 지적별 반영·반박 (반복 2회차)

판정 요약: Critical 3 전부 반영, Major 9 중 반영 7, 부분 반영(코드는 고치고 확인은 HIL) 2(M-02, M-05는 "수정 또는 HIL 절차 명시" 조건에 따라 절차 명시), 반박 0. Minor 10 중 반영 8, 문서/HIL 대응 2(m-08, m-10).
펌웨어 C 변경은 ESP-IDF가 없어 **빌드·실행하지 못했다**(호스트 로직 시험과 소스 형태 시험만 통과). 새로 쓴 IDF API 필드(`pcnt_unit_config_t.intr_priority`, `gptimer_config_t.intr_priority`, `ESP_INTR_FLAG_LEVELn`)는 헤더와 대조하지 못했다 -> §6 목록에 추가.

### Critical

| ID | 결과 | 반영 내용 (파일) | 증명 시험 |
|---|---|---|---|
| C-01 | 반영 | `judge.py`: `_precheck`가 T-03~T-09 공통으로 `wait_status == DONE`, `timed_out == False`, `seq_done == (RUN_SEQ ? repeat : 1)`, 파라미터 쓰기 후 status != 0x11, 쓴 값 읽기 일치, CLEAR_STATS 성공(카운터 0), SEED 불변, 필수 키 존재를 요구. 거부(0x10/0x11/0x12)·중단(IDLE)·타임아웃·seq_done 부족은 FAIL, 키 누락은 NOT_RUN. T-04/05/06은 2축(axis_mask 3)과 반복 >= 1,000(architecture 6.1 "축당 1,000회" [가정])도 요구, 미달이면 NOT_RUN. `scenario.py`: 실행 전 STOP_ALL로 묵은 status 제거, CLEAR_STATS 확인, 입력 전부 쓰기 후 status와 값 읽기 대조(`_write_params`). T-08·T-09·T-03도 같은 전제를 적용(거부된 실행 + 묵은 LA 파일 = 거짓 합격 방지). | `test_judge_regress.py::test_c01_*`(매개변수 조합 포함 다수), `test_e2e_t04_rejected_run_is_fail`, `..._aborted_...`, `..._timeout_...`, `..._out_of_range_parameter_...`(모의 장치 모드: reject/abort/hang/0x11), `test_logic.c::t_r1_seq_aborted...` |
| C-02 | 반영 | `judge.py`: 외부 계수가 없으면(또는 기대값이 없으면) T-04/05/06은 **NOT_RUN**("PCNT=목표는 출력 핀 자체를 센 값이라 누락을 검출하지 못함"), 계수가 틀리면 FAIL. `expect.py`: 호스트가 펌웨어와 같은 xorshift32와 소비 순서로 **축별** 목표 합을 재현(`expected_edges`), `analyse_la`가 축별 기대 엣지 리스트로 비교(스칼라는 모든 축에 적용, 리스트는 축별). 펌웨어 출력 레지스터를 추가하는 대안은 설계 주소 배정 이슈(0x7040 영역)를 피하려고 택하지 않음. | `test_expect.py`(재현 값 = 실제 C `tfw_seq_run`이 하드웨어에 준 목표 합, 7개 조합: 무작위 2축/1축/seed 0/RUN_MOVE/RUN_FIXED), `test_c02_per_axis_expectation_random_sequence`(두 축 기대값이 서로 다름 = 옛 코드의 단일 값 비교는 정상 장치에서도 불일치), `test_e2e_t04_lost_pulse_seen_only_externally`(PCNT 오차 0인데 외부 1펄스 부족 -> FAIL), `test_e2e_t04_without_external_count_is_not_pass` |
| C-03 | 반영 | T-05: `refused == 0`, 부하 `requests > 0`, 실행 중 `MB_CLIENTS` 최댓값 >= 1(제어)+부하 수+HMI, 빌드 `max_clients`(새 읽기 전용 레지스터 0x705E, 아래 설계 차이 참고) >= 필요 접속 수를 요구. 부하 4개 미만 또는 `hmi` 미지정이면 NOT_RUN. `load.py`: 첫 요청에서 연결이 닫히면 거부로 계산(펌웨어는 accept 후 close). T-06: `nvs_writes >= max(1, 실행시간/주기 x 0.5 [가정])`, `iram_safe`는 장치 `cfg_flags` b0(0x7012)로 판정하고 시나리오 값이 다르면 FAIL. | `test_c03_*`, `test_e2e_t05_default_max_clients_4_is_fail_not_pass`(기본 4 빌드 모의 장치: 거부 > 0 -> FAIL), `test_e2e_t05_with_enough_connections_passes`, `test_e2e_t06_nvs_load_must_run`(장치가 off 빌드면 FAIL), `test_c03_t06_iram_flag_comes_from_device`, `t_r1_max_clients_register` |

### Major

| ID | 결과 | 반영 내용 | 증명 |
|---|---|---|---|
| M-01 | 반영 | `tfw_es_trip`을 `tfw_logic.h`의 `always_inline`으로 옮겨 IRAM의 `es_isr` 안에 컴파일되게 함(`tfw_core.c`의 플래시 사본 삭제). ISR 안의 `esp_timer_get_time()`을 `esp_cpu_get_cycle_count()`로 교체(트립 후 10 ms 판정은 태스크가 사이클 차이로 계산). `tfw_estop_input_active`에 `IRAM_ATTR`. `ESP_INTR_FLAG_IRAM`은 `CONFIG_PCNT_ISR_IRAM_SAFE` 빌드에서만 사용(iram_off 빌드는 일반 ISR). ISR 안에서 부르는 `tfw_pulse2_stop`/`count_isr`은 1회차부터 IRAM_ATTR. ISR 경로의 인라인 `tfw_upd_record`/`tfw_profile_freq`/`tfw_sat_mul`도 always_inline. `check_iram.sh` 목록에 `tfw_es_trip`, `tfw_upd_record`, 드라이버 함수(`pcnt_unit_get_count`, `mcpwm_generator_set_force_level` 등) 추가. **남은 확인(HIL)**: iram_on 빌드에서 드라이버 함수가 실제 IRAM에 있는지는 `check_iram.sh` 결과로만 알 수 있다. | `test_firmware_static.py::test_m01_*`(es_isr 호출 집합 제한, 인라인 정의, IRAM 플래그 조건부) |
| M-02 | HIL 절차 명시(코드 미변경) | ISR 안 FPU 사용 가부는 IDF 5.5 문서로 확인하지 못했다(검토자와 동일, **미확인**). 확인 전에는 HWTIMER 모드의 첫 이동에서 패닉이 나면 `CONFIG_TFW_UPD_MODE_HWTIMER=n`(10 ms 태스크)으로 바꿔 비교한다. 정수 연산 전환은 MOD-07 연산 형식 결정(이슈 5)과 묶여 있어 임의로 바꾸지 않음. m-07의 `-fno-math-errno`만 적용. | HIL 첫 빌드: HWTIMER 모드로 1회 이동 시 패닉·레지스터 깨짐 유무 |
| M-03 | 반영(설정화) | `Kconfig`: `TFW_IRQ_PRIO_ESTOP`(기본 3) > `TFW_IRQ_PRIO_PCNT`(2) > `TFW_IRQ_PRIO_GPT`(1), 0이면 드라이버 기본값. PCNT 유닛·GPTimer·GPIO ISR 서비스에 각각 적용. 부팅 로그에 레벨과 ISR 코어(비상정지는 app_main 코어, PCNT/GPT는 갱신 코어) 출력. 기본값 대 명시값 비교는 빌드 옵션(세 값을 0으로)으로 가능하며 T-07·T-08 결과에 우선순위 구성을 조건으로 적어야 한다. 단, 레벨 번호가 P4에서 실제 우선순위로 해석되는 방식과 비상정지 ISR이 다른 코어에 놓일 때의 영향은 **미확인**. | `test_firmware_static.py::test_m03_*`(ESTOP > PCNT > GPT > 0, 필드 사용) |
| M-04 | 반영 | `tfw_es_boot_check`: 초기화 때 입력이 이미 활성이면 즉시 래치. `tfw_es_blocking`(래치 또는 입력 활성)을 `tfw_cmd_check` 호출, 시퀀서 abort 판정, 레거시 START 모두에 사용(`tfw_estop_blocking()`). | `t_r1_estop_active_at_boot_latches_and_blocks`, `t_r1_input_active_blocks_even_when_not_latched`, `test_m04_*` |
| M-05 | HIL 절차 명시 | 정지 상태 첫 주기 적용은 코드 확인 불가. `scenario_example.yaml` 첫 항목에 "1 MHz 운전 후 5 kHz로 시작, 첫 2~3주기를 LA로 확인" 단계를 넣었다. 문제가 확인되면 정지 중 즉시 갱신 경로로 되돌린다(수정 방향 합의, HIL 결과 후 진행). | HIL(LA 캡처) |
| M-06 | 반영 | `tfw_upd_ref_us(task_mode, cfg_us)`: 태스크 모드 기준 주기 10,000 µs 고정. `tfw_pulse2_init`이 부팅 로그에 "update reference period"와 모드를 남긴다. 결과 레지스터에는 모드 항목이 없다(주소 배정 이슈, 이슈 제기 3). | `t_r1_task_mode_reference_period`(10 ms 주기에서 late 0, 옛 기준은 99건, 16 ms 간격은 1건 검출) |
| M-07 | 반영 | 부하의 FC16이 SEED를 **현재 값으로 되쓰기**(시작 시 읽은 값)만 하도록 변경. 실행 후 SEED를 다시 읽어 `seed_changed`로 기록, 바뀌면 FAIL. | `test_m07_tcp_load_never_changes_seed`(모의 장치 SEED가 777 유지), `test_m07_seed_change_during_run_is_fail` |
| M-08 | 반영 | 축2 핀과 probe 핀 기본값 -1(축2 미생성 -> 0x12, probe 생략). 부팅 때 `tfw_pin_conflict`로 축1·축2·비상정지·디버그·probe·기존 사용 핀(51, 23, 26, 27, 46) 중복 검사, 겹치면 해당 축/probe를 만들지 않음. EMAC/RMII 핀 목록은 **미확인**이라 검사에 넣지 못함(보드 회로도 확인 필요). | `t_r1_pin_conflict`(옛 기본값 probe 2 = 축2 PLS 2 충돌 재현), `test_m08_*` |
| M-09 | 반영 | `relaunch_after_latch`를 측정: `latch_probe` 단계가 래치 중 RUN을 보내 0x10 거부, 상태 RUNNING/DONE 아님, `seq_done` 불변을 확인(`probe_latched_run`). 래치가 아니면 측정 불가(None) -> 보고서에 "미측정(합격 근거 아님)". 핀 펄스 0 확인은 LA 몫이라고 사유에 적음. | `test_m09_*`(래치/비래치/시작됨 3경우, 미측정 문구, 모의 장치 종단) |

### Minor

| ID | 결과 | 내용 |
|---|---|---|
| m-01 | 반영 | `tfw_sat_mul`(`__builtin_mul_overflow`, 인라인) 두 번 적용. `t_r1_sat_mul_no_wrap` |
| m-02 | 반영 | 측정한 CPU MHz를 `tfw_pulse2_set_cpu_mhz`/`tfw_estop_set_cpu_mhz`로 전달 |
| m-03 | 반영 | 명령 처리(`tfw_handle_cmd`, `on_effect`)를 뮤텍스로 직렬화, 시퀀서 실행 중 레거시 START 거부(`seq_busy` 훅) |
| m-04 | 반영 | 2번째 제너레이터/채널 probe가 `ESP_ERR_INVALID_ARG`이면 "판정 불가, 자원 부족 아님"으로 로그. probe 핀 미지정이면 건너뜀 |
| m-05 | 반영 | T-03 합격을 10 s **평균** 주파수 >= 200 kHz(허용오차 0.01% [가정], 기록)로 판정, 최저 순간 주파수는 기록만. 199.5 kHz는 FAIL. `test_m05_*` |
| m-06 | 반영 | 시나리오 `la_trigger_edge`(기본 fall), `estop_active_low`와 다르면 `la_trigger_edge_mismatch` -> T-07 NOT_RUN. `test_m06_*` |
| m-07 | 반영 | 프로젝트 `idf_build_set_property(COMPILE_OPTIONS "-fno-math-errno" APPEND)`. 효과는 빌드 후 `check_iram.sh`로 확인(미실행) |
| m-08 | 문서(HIL) | 경로 A 단독 측정 때 OST 브레이크 영향 유무는 LA로 먼저 확인해야 한다(미확인). 영향이 있으면 경로 A 시험은 fault를 바인딩하지 않은 빌드로 한다 |
| m-09 | 반영 | `clear_stats`는 요청 플래그만 세우고 NVS 태스크가 스스로 지움(태스크를 알림으로 깨움). `test_m09_clear_stats_...` |
| m-10 | 이슈 제기 | architect 판단 사항(아래 이슈 3) |

### 설계와 다른 점 (2회차 추가)

| # | 설계서 | 구현 | 사유 |
|---|---|---|---|
| 14 | TFW-03 시험 창에 `max_clients` 출력 없음 | 읽기 전용 U16 0x705E(`mb_max_clients`) 추가 | 리뷰 C-03이 빌드의 max_clients를 레지스터로 내보내라고 권고. 0x705E는 설계서에서 비어 있는 주소(히스토그램 0x7056~0x705D 다음). 설계서는 수정하지 않았다 |
| 15 | TFW-05 판정 입력 | 판정 입력에 실행 상태·파라미터 읽기 값·접속 수 등이 필수 | C-01~03 |
| 16 | 축2 핀 기본값 2/3/4 | 기본 -1 | M-08 |

### 이슈 제기 (2회차 갱신, 설계 모순은 설계서를 고치지 않음)

1. **T-05 접속 수 6개 대 최대 4개(설계 모순)**: architecture 6.1 T-05는 제어 1 + 부하 4 + HMI 1 = 6이고 TFW-03은 최대 4이며 5번째 거부가 완료 기준이다. 판정은 이 모순으로 거짓 합격이 나지 않게 처리했다: 기본 빌드(4)에서는 접속 거부 또는 `max_clients < 6`으로 **FAIL**, 부하 4개 미만이나 HMI 미지정이면 **NOT_RUN**. 어떤 경우에도 PASS가 나오려면 6개 접속이 실제로 관측돼야 한다. 결정 후보(사용자 확정 필요, 리뷰와 동일): (a) 제품 `max_clients`에 제어·HMI를 포함해 기본값을 6 이상으로 올린다, (b) 4를 유지하고 T-05 조건을 "부하 2 + HMI 1 + 제어 1"로 바꾼다. 판정 상수 `T05_MIN_CLIENTS`가 (b) 선택 시 바뀐다.
2. **0x7040 영역 방향(설계 모호)**: TFW-01 0x7004 행의 "축별은 0x7040~"과 출력 행 0x7040~ "축별 마지막 목표·PCNT"가 충돌. 출력 전용으로 구현했고 외부 계수 기대값은 호스트 재현(`expect.py`)으로 풀어 이 영역에 입력이 필요 없게 했다. 설계서의 0x7004 행 문구 정정과 `probe_caps` 결과·모드(HWTIMER/TASK) 출력 주소 배정이 필요하다(architect).
3. **축2 핀 임시값**: 기본값을 -1로 바꿔 임시 핀으로 바로 구동하지 않게 했다. 실제 핀은 PS-27 이후 사용자가 지정해야 하며 EMAC/RMII 사용 핀과의 중복 검사는 목록이 없어 하지 못했다. `es_isr_lat_ns` 내부 측정 없음(m-10)은 "외부 측정만 인정"으로 할지 architect가 정해야 한다.
4. **ISR 우선순위 배치(architect)**: 비상정지 > 감시점 > 갱신 > 통신 순서를 TFW-02/04 제약에 명시할지(T-07·T-08 해석의 전제). 현재는 Kconfig 기본값으로 구현했고 P4에서의 효과는 미확인.
5. **ISR 안 FPU(M-02)**와 MOD-07 연산 형식: 1회차 이슈 5에 "IDF 5.5 FreeRTOS(IDF) 문서의 FPU 사용 제한 확인 필요"를 추가한다.
6. 위 모든 펌웨어 변경은 **빌드되지 않았다**. 첫 빌드 오류는 §6 목록과 이번에 추가한 `intr_priority` 필드, `esp_cpu_get_core_id` 사용 부분부터 확인한다.

## 9. 리뷰 2차(`reviews/test-fw-review-2.md`) 지적별 반영·반박 (반복 3회차, 마지막)

판정 요약: 지적 Critical 1, Major 5(N-01, N-02, N-03, R-01, M-05), Minor 8 중 **반박 0**. Critical·Major 6건 전부 코드와 시험으로 반영. Minor 8 중 반영 4(n-1, n-2, n-3, n-8), 부분 반영 1(n-5), 이슈 제기·보류 3(n-4, n-6, n-7). 펌웨어 C는 여전히 **ESP-IDF 빌드·실장비 실행 없음**이다. 이번에 추가된 것은 스텁 문법 검사와 호스트 빌드 시험뿐이며, 스텁의 IDF 시그니처는 기억 기반이라 실제 헤더와 대조되지 않았다.

### Critical / Major

| ID | 결과 | 반영 내용 | 증명 시험 (2회차 코드에서 실패 확인) |
|---|---|---|---|
| N-04 | 반영 | **실행과 판정 분리.** `run_scenario`는 `la_csv`가 있는 단계를 운전 후 **PENDING**으로 남기고 `run_start_ts`/`run_end_ts`/`run_s`를 JSON에 저장. 새 CLI `judge <result.json> <csv>`(또는 `run --wait-capture`의 대화형 정지, 또는 `capture` 훅)가 CSV를 붙이며 `capture_check`로 (1) CSV 수정 시각 > 운전 종료, (2) 첫~끝 행 길이 >= run_s - 0.5 s [가정], (3) sha256이 다른 실행 기록에 없음을 확인. 하나라도 어기면 `la_fresh=False`. 판정은 `_la_gate`가 LA 계열 키(`la_edges` 등)가 있는데 `la_fresh`가 True가 아니면 FAIL("묵은 캡처"). T-03/04/05/06/07/09 모두 적용. 보고서는 PENDING을 "판정 대기"로 표기. `scenario_example.yaml` 머리말과 §4 6a에 절차 기재 | `test_n04_*` 6건: 증거 없는 LA 데이터 FAIL(5 T-ID), **운전 전에 만든 CSV**는 PENDING 또는 FAIL, **같은 CSV 재사용** FAIL, 짧은 캡처 FAIL, run -> judge 흐름 PASS/FAIL, CLI 존재 |
| N-01 | 반영 | 새 `tfw_logic/include/tfw_cfg.h`: 항상 정의되는 0/1 보조 매크로(`TFW_B_UPD_MODE_HWTIMER`, `TFW_B_ESTOP_ACTIVE_LOW`, `TFW_B_LEGACY_NVS_ON_WRITE`, `TFW_B_PCNT_ISR_IRAM_SAFE`)를 `#if defined(...) && ...`로 정의(`sdkconfig.h`를 먼저 include). `tfw_main.c`, `tfw_estop_io.c`의 C 식 사용을 모두 교체. bool 옵션은 `#if`에서만 쓴다 | `test_cfg_syntax.py`: Kconfig를 읽어 **ESP-IDF 규칙**(int은 항상 정의, bool y는 1, n은 미정의)으로 `sdkconfig.h`를 만들고 7개 조합(기본, iram_on, iram_off, TASK 모드, active_high, legacy_nvs, iram_off+TASK+active_high) x 2개 파일(`tfw_main.c`, `tfw_estop_io.c`)을 `gcc -fsyntax-only`. 자체 검증으로 결함 코드가 실제로 실패함도 확인. `test_n01_bool_config_macros_are_used_only_in_preprocessor_conditions`: 모든 C/H 파일에서 bool `CONFIG_`가 `#if` 밖에 없음 |
| N-02 | 반영 | `tfw_estop_init`을 래퍼로 감싸 **성공했을 때만** `s_init_ok = true`. `tfw_estop_blocking()`은 `!s_init_ok ||` 래치/입력 활성. 초기화 전·실패(gpio_config, ISR 서비스, 핸들러 등록, 태스크 생성) 시 RUN·레거시 START 거부(0x10). 실패는 `es_last_err`에, 서비스 기설치(레벨·IRAM 플래그 미적용)도 `es_last_err = ESP_ERR_INVALID_STATE`로 내보냄(차단은 안 함). `app_main` 오류 로그에 "ALL motion commands are refused" 명시. **판정**: T-07은 `es_last_err == 0`과 실행 중 `es_trip_count` 증가(`es_trip_count_before`는 운전 전에 읽음)를 요구, 아니면 NOT_RUN | `estop_host/test_estop_host.c`: 실제 `tfw_estop_io.c`를 호스트에서 컴파일하고 IDF 호출을 제어 가능한 스텁으로 바꿔 실행(29건): 초기화 전 차단, 4가지 단계 실패 각각 차단, 정상 초기화는 차단 안 함, 서비스 기설치 `last_err` 노출, 부팅 시 활성이면 래치. `test_n02_*` 3건(판정·모의 장치) |
| N-03 | 반영 | (a) `es_isr`: 래치(`tfw_es_trip`)를 **정지보다 먼저** 수행. (b) `start_common`: 시작 전에 `s_block_fn()`(= `tfw_estop_blocking`, `tfw_pulse2_set_block_fn`으로 main이 등록)이면 거부 -> `pcnt_unit_clear_count`가 트립 후 계수를 지우지 않음. 타이머 시작 **후** 한 번 더 확인해 래치면 `tfw_pulse2_stop`. (c) `hw_move`/`hw_fixed`가 차단 중이면 -1. (d) `tfw_seq_run`: 반복 시작 시와 **축별 시작 직전** `aborted()` 확인, 시작이 트립 때문에 거부되면 오차가 아니라 중단으로 처리 | C: `t_r2_seq_no_move_after_trip_between_iterations`(반복 사이 트립 뒤 move 호출 0건), `t_r2_seq_no_second_axis_start_after_trip`, `t_r2_seq_start_refused_by_trip_is_abort_not_pulse_error`. 호스트: `t_isr_latches_before_it_stops`(정지 시점에 래치 이미 true). 정적: `test_n03_*` 3건. **한계**: `start_common`의 실제 경쟁(ISR 끼어들기)은 호스트로 재현 못 함, 소스 형태 확인만 |
| R-01 | 반영 | 실행 중 `MB_CLIENTS` **최솟값**(`mb_clients_min`; 안정화 `settle_s` 기본 1 s 이전 샘플 제외, 마지막 샘플은 항상 포함)을 기록하고 `min >= 1+부하+HMI`를 합격 전제로 함. 부하 클라이언트별 `dropped`(운전 중 연결 끊김)·`idle_clients`(요청 0건)를 세어 0이 아니면 FAIL, `errors > 0`은 사유로 기록. 최솟값 미측정이면 NOT_RUN | `test_r01_*` 3건(최솟값 3이면 FAIL, 키 없으면 NOT_RUN, dropped/idle FAIL, 실제 연결을 끊는 가짜 서버에서 `dropped == 1`, e2e) |
| M-05 | 반영(HIL 확인은 남음) | `scenario_example.yaml`: 선행 `RUN_FIXED 1 MHz` -> 5 kHz -> 다시 1 MHz 3단계. `la.first_periods`와 `analyse_la(la_first_periods)`: 첫 n(기본 3)주기가 기대 주기(res 올림 틱) ± LA 2샘플 안이어야 하며 **짧은 쪽·긴 쪽 모두** FAIL(긴 쪽 사유 "이전 주기 잔류"). 주기 수 부족이면 NOT_RUN. CSV는 N-04 규칙으로 붙임 | `test_m05_*` 5건: 5 kHz 뒤 짧은 첫 주기 FAIL, **1 MHz로 돌아올 때 긴 첫 주기 FAIL(2회차 규칙 `requests_exceeded`는 []로 못 잡던 경우)**, 주기 부족 NOT_RUN, 예시 시나리오 순서 |

### Minor

| ID | 결과 | 내용 |
|---|---|---|
| n-1 | 반영 | 시나리오에 `la_sample_rate_hz`가 있으면 "요청 초과" 한계를 LA 샘플 1개 분해능으로 계산(`1/(1/f - 1/fs)`)하고 근거를 `freq_tol_basis`로 기록. 없으면 `tol_pct`(가정) 유지. T-09 예시 단계는 50 MS/s [가정]를 지정. `test_n1_*` |
| n-2 | 반영 | 첫 SEED 읽기가 타임아웃이면 FC16을 보내지 않고 종료(idle로 집계). `test_n2_first_seed_read_timeout_does_not_write_zero` |
| n-3 | 반영 | `check_iram.sh` 점검 목록에 `mcpwm_timer_start_stop` 추가, IRAM이 아니면 경로 A를 force-level만으로 바꾸는 판단 기준을 주석에 명시. 판단은 첫 실측 후. `test_n3_*` |
| n-4 | 이슈 제기 | 빌드 극성을 읽는 레지스터는 주소 배정이 architect 몫(리뷰 이슈 3). 시나리오 값끼리의 대조는 유지, 장치 값 대조는 주소 배정 후 |
| n-5 | 부분 반영 | 시퀀서·레거시 START가 같은 축을 동시에 시작하는 경쟁은 `hw_move`/`hw_fixed`에서 차단 검사를 추가한 것 외에는 **그대로**. 축 단위 뮤텍스는 설계에 없는 구조 변경이라 보류, 이슈로 남김 |
| n-6 | 보류 | 옵션별 IRAM 비트는 `cfg_flags`의 의미 변경(주소·비트 배정은 architect). 대신 §4 절차 3의 sdkconfig grep과 부팅 로그의 IRAM 옵션 줄을 보고서에 첨부하도록 유지 |
| n-7 | 보류 | 외부 토털라이저 입력(`ext_edges`)은 장비 결정(리뷰 이슈 4, 사용자)이 먼저. 지금은 LA 연속 캡처가 안 되면 T-04/05/06이 NOT_RUN으로 남는다 |
| n-8 | 반영 | `tfw_pulse2_clear_stats`는 `s_upd_clear_req` 플래그만 세우고 `update_all`(유일한 쓰기 주체)이 지움. `test_n8_*` (`isr_lat_ns_max`는 ISR이 쓰는 값을 태스크가 0으로 쓰는 구조가 남아 있음: 갱신 손실은 CLEAR 직후 한 번 가능) |

### 설계와 다른 점 (3회차 추가)

| # | 설계서 | 구현 | 사유 |
|---|---|---|---|
| 17 | TFW-05: 실행 후 LA CSV로 판정 | 실행(PENDING)과 판정(`judge`)이 분리, 결과 JSON에 `run_start_ts`/`run_end_ts`/`la_sha256`/`la_fresh` 추가, CLI `judge` 추가 | N-04 |
| 18 | TFW-04: 초기화 실패 때의 동작 미정 | 초기화가 끝나기 전·실패 시 모든 운전 명령 거부(영구 차단) | N-02. 리뷰 이슈 2(architect)가 확정 전까지의 안전한 쪽 구현 |
| 19 | TFW-02: 시작 API에 비상정지 연동 없음 | `tfw_pulse2_set_block_fn` 추가(시작 전·후 래치 확인) | N-03 |
| 20 | (시험 도구) | 시험 폴더에 ESP-IDF 스텁(`tests/test-fw/idf_stub/`)과 호스트 빌드 시험(`estop_host/`) 추가 | N-01/N-02 증명. 스텁은 기억 기반이라 실제 API 정합성을 증명하지 않음 |

### 남은 위험 / 이슈 제기 (3회차)

1. ESP-IDF 빌드·HIL은 여전히 한 번도 실행되지 않았다. 스텁 문법 검사는 bool 옵션 매크로와 두 파일(`tfw_main.c`, `tfw_estop_io.c`)만 다룬다. `tfw_pulse2.c`, `tfw_modbus.c`는 스텁 범위 밖(EMAC/lwIP/GPTimer 구조체가 많음)이며 이들의 bool 사용은 정적 시험으로만 확인된다.
2. `start_common`의 ISR 경쟁(N-03 a)은 "래치 선행 + 시작 후 재확인"으로 닫았다고 판단하지만, 다른 코어에서 병렬로 실행될 때의 모든 인터리빙을 증명한 것은 아니다. HIL T-07에서 트립 후 펄스 0을 LA로 확인해야 한다.
3. LA 캡처 길이 검사의 여유 0.5 s와 안정화 1 s, 주기 허용 2샘플은 [가정]이다. CSV에 유휴 구간이 기록되지 않으면 길이가 실제 캡처보다 짧게 보이므로(보수적), 정상 캡처가 FAIL이 될 수 있다 -> 첫 실측에서 조정.
4. (architect) 리뷰 이슈 2·3(초기화 실패 동작, 빌드 극성·옵션별 IRAM 비트의 주소)과 (사용자) 이슈 4(외부 계수 장비)는 열려 있다.
