# 코드 검토: 시험 펌웨어 TFW-01~05 (D-025 예외, G3 전 실측용) — 1차

| 항목 | 내용 |
|---|---|
| 검토 대상 | `project/04_code/test-fw/`(펌웨어·호스트 도구), `project/04_code/tests/test-fw/`, `project/04_code/notes/test-fw.md` |
| 기준 | `03_design/modules/TFW-01~05`(v0.1), `03_design/architecture.md` §6.1(T-03~T-14 판정 기준), `03_design/interface-spec.md` §5.3(max_clients), decisions D-024~D-028 |
| 대조 코드 | `/home/user/servo1axis`(읽기 전용, `components/motion_core/motion_core.c`, `components/movion_modbus/movion_modbus_tcp.c`, `main/app_main.c`) |
| 검토자 | code-reviewer, 2026-10-30 |
| 테스트 결과 | 검토자는 실행하지 않았다. notes §3 기록(C 31개 함수·검사 16,553건 실패 0, pytest 30 통과)과 메인 세션 재실행 결과(checks=16553 failed=0, 30 passed)를 인용만 한다. ESP-IDF 빌드·HIL은 한 번도 실행되지 않았다(notes §0, §7-8). |

## 판정: **재작업**

- Critical 3, Major 9, Minor 10.
- 펌웨어 쪽 핵심 구조(축 배열화, ceil 규칙, NVS 저장 제거, 완료 플래그 방식)는 설계와 맞고 servo1axis 대비 개선이다.
- 재작업 사유는 **호스트 판정 로직의 거짓 합격 경로 3건(C-01~03)**이다. 이 결과가 G3 근거로 그대로 들어가므로, HIL 결과를 G3 보고서에 쓰기 전에 반드시 고쳐야 한다. 펌웨어 쪽 Major(M-01~M-06)는 HIL 첫 빌드 전에 고칠 것을 권고한다.

---

## 1. 지적 항목

### Critical

| ID | 파일:줄 | 근거 | 권고 |
|---|---|---|---|
| C-01 | `host/tfw_host/judge.py:35-60`, `host/tfw_host/scenario.py:108-111`, `:58-73` | `run_step`은 CLEAR_STATS 후 RUN을 보내고 `_wait_done`이 status ≠ RUNNING이면 바로 끝난다. 그런데 `judge()`는 `wait_status`, `timed_out`, `seq_done`을 **전혀 보지 않는다**. 그래서 (a) 축2 자원 실패로 0x12 거부, (b) 래치 중 0x10 거부, (c) 비상정지·STOP으로 중단(IDLE, seq_done < repeat), (d) 타임아웃이 난 경우에도 err/overshoot 카운터가 0이면 T-04·T-05·T-06이 **PASS**가 된다. 키가 없을 때도 `d.get(..., 0)`이라 PASS 쪽으로 기본값이 들어간다. architecture §6.1 T-04 "축당 1,000회", T-05 "T-04 반복 중"과 맞지 않는다. | 판정 전 전제조건을 검사한다: `wait_status == DONE(2)`, `timed_out == False`, `seq_done == repeat`(RUN_SEQ), 필수 키 존재. 하나라도 어긋나면 FAIL(또는 NOT_RUN)로 하고 사유를 남긴다. 파라미터 쓰기 뒤 status 0x11 확인도 넣는다(현재는 범위 밖 값이 무시된 채 이전 값으로 실행된다, `tfw_regwin.c:114`). 이 경우들의 pytest 음성 시험을 추가한다(현재 `test_scenario_report.py:86-98`에 없음). |
| C-02 | `host/tfw_host/judge.py:40-44`, `host/tfw_host/scenario.py:139-140`, `components/tfw_logic/tfw_seq.c:27-31` | T-04에서 외부 계수가 없으면(`ext is None`) 사유만 적고 **PASS**를 낸다. architecture §6.1 T-04 기준은 "외부 계수 = PCNT = 목표", TFW-02 완료 기준 #3도 "외부 계수 = 목표"다. PCNT는 같은 핀 루프백이며 정지 트리거 자신이라 PCNT=목표는 거의 항상 성립한다(누락 검출력이 없다). 더해서 `external_count_matches`는 두 축 엣지 수를 **같은 한 값**(`la_expected_edges`)과 비교하는데, RUN_SEQ 무작위 모드는 축마다 다른 거리를 뽑으므로(같은 rng를 축 순서대로 소비) 정상이어도 일치할 수 없다. 사용자는 결국 LA를 빼고 돌리게 되고 그러면 PASS가 나온다. | `ext is None` → NOT_RUN(또는 "PCNT만, 판정 보류"). 기대 엣지 수는 축별로 받는다. 무작위 시퀀스의 축별 목표 합은 펌웨어가 내도록 출력(예: 축별 누적 목표 합 U32×2)을 추가하거나, 호스트가 같은 xorshift로 재현해 계산한다(`tfw_core.c:50-72`와 동일 구현 + 대조 시험). |
| C-03 | `host/tfw_host/judge.py:45-60`, `host/tfw_host/scenario.py:104-107,166`, `host/tfw_host/load.py:52-61`, `main/Kconfig.projbuild:64-69` | 부하가 실제로 걸렸는지 검증하지 않는다. (a) T-05: `tcp_load.refused/errors/requests`를 판정에 쓰지 않는다. 기본 빌드(`TFW_MAX_CLIENTS=4`)에서 시나리오는 제어 연결 1 + 부하 4 = 5개를 열어 부하 1개가 거부되지만(`tfw_modbus.c:272-276`) PASS가 난다. (b) T-06: `nvs_writes > 0`을 확인하지 않아 NVS 부하가 꺼진 채여도 PASS. (c) T-06 `iram_safe`는 시나리오 파일 값(`scenario.py:166`)이고 장치의 `cfg_flags` b0(0x7012)를 읽지 않아, IRAM off 빌드를 on으로 잘못 적으면 off 결과가 "on 합격"으로 기록된다. | T-05: `refused == 0`, 부하 `requests > 0`, 실행 중 `mb_clients ≥ 기대 접속 수`를 PASS 전제로 한다. T-06: `nvs_writes ≥ 기대치(운전 시간/주기의 일정 비율)`를 전제로 한다. `iram_safe`는 장치 0x7012 b0에서 읽고 시나리오 값과 다르면 FAIL. 빌드의 max_clients도 부팅 로그·레지스터로 내보내 보고서에 적는다. |

### Major

| ID | 파일:줄 | 근거 | 권고 |
|---|---|---|---|
| M-01 | `components/tfw_estop_io/tfw_estop_io.c:41-55,130`, `components/tfw_logic/tfw_core.c:83-89`, `components/tfw_logic/CMakeLists.txt:1-4`, `tools/check_iram.sh:8` | 비상정지 ISR은 `gpio_install_isr_service(ESP_INTR_FLAG_IRAM)`로 등록돼 플래시 캐시가 꺼진 동안에도 실행된다. 그런데 ISR이 부르는 `tfw_es_trip()`은 `tfw_core.c`의 일반 함수(IRAM_ATTR도 linker fragment도 없음)라 **플래시에 있다**. NVS 부하(T-06)·cfg 저장 중에 GPIO19 엣지가 오면 캐시 접근 오류로 패닉할 수 있다. `sdkconfig.tfw_iram_off` 빌드에서는 IRAM 플래그 ISR이 플래시 상주 드라이버 함수(`pcnt_unit_get_count`, `mcpwm_generator_set_force_level`, `gpio_set_level`)까지 부른다. `check_iram.sh` 점검 목록에 `tfw_es_trip`이 없어 잡히지 않는다. 설계 TFW-04 §1-1 "IRAM", TFW-02 §4 "ISR·갱신 콜백은 IRAM 배치"를 어긴다. | `tfw_es_trip`을 ISR 파일 안 `static inline`/IRAM_ATTR로 옮기거나 tfw_logic에 linker fragment(`.lf`)를 추가한다. iram_off 빌드에서는 GPIO ISR을 IRAM 플래그 없이 설치(조건부 컴파일)해 비교 조건을 맞춘다. `check_iram.sh` 목록에 `tfw_es_trip`, `tfw_upd_record`(인라인 확인용)를 추가한다. |
| M-02 | `components/tfw_pulse2/tfw_pulse2.c:127-146`, `components/tfw_logic/include/tfw_logic.h:50-62`, `main/Kconfig.projbuild:40-42` | 기본값 `TFW_UPD_MODE_HWTIMER=y`에서 GPTimer ISR이 `tfw_profile_freq()`(float32 곱셈·`__builtin_sqrtf`)를 실행한다. notes §2-2가 "P4는 단정도 FPU가 있다 [가정]"이라고 했지만, 남은 쟁점은 FPU 유무가 아니라 **ESP-IDF가 ESP32-P4 인터럽트 문맥에서 FPU 사용을 허용하는지**다(FreeRTOS(IDF)의 FPU 지연 문맥 저장 방식 때문에 ISR 안 FPU 사용이 제한될 수 있다. 검토자도 IDF 5.5 문서로 확인하지 못했으므로 **미확인**). 허용되지 않으면 첫 이동부터 illegal instruction 패닉이 나거나 태스크 FPU 레지스터가 깨진다. | IDF 5.5 문서 "FreeRTOS (IDF) — Floating Point Usage"와 P4 인터럽트 진입 코드로 확인한다. 확인 전에는 ISR 경로를 정수 연산(정수 제곱근 또는 룩업표)으로 바꾸거나, HWTIMER 콜백이 고우선 태스크를 깨우고 계산은 태스크가 하도록 바꾼다. notes 이슈 5(MOD-07 연산 형식)에 이 제약을 넣는다. |
| M-03 | `components/tfw_pulse2/tfw_pulse2.c:209-216,249-256`, `components/tfw_estop_io/tfw_estop_io.c:130-132`, `main/tfw_main.c:203-214,277` | 인터럽트 우선순위를 하나도 지정하지 않는다(`pcnt_unit_config_t.intr_priority`, `gptimer_config_t.intr_priority`, GPIO ISR 서비스 `ESP_INTR_FLAG_LEVELn` 모두 기본값). PCNT 인터럽트와 GPTimer 인터럽트는 같은 `pulse_init_task`(갱신 코어)에서 할당돼 같은 코어·같은 기본 레벨에 놓인다. 그래서 감시점 ISR(`on_reach`)이 2축 갱신 ISR이 끝날 때까지 기다리고, 그 시간만큼 1 MHz에서 µs당 1펄스가 초과된다. 이것은 설계가 재려는 지연(T-08)이 아니라 시험 펌웨어의 배치에서 생기는 지연이다. 비상정지 ISR(GPIO19)도 다른 ISR보다 우선하지 않는다. 배치안 B(`tfw_main.c:231`)에서는 app_main 코어(0)에 GPIO ISR과 갱신·PCNT ISR이 함께 놓인다. 요청 (3)의 "우선순위" 관점에서 보장되는 것이 없다. | 우선순위를 명시한다: 비상정지 GPIO > PCNT 감시점 > GPTimer 갱신 > EMAC. 할당 코어와 레벨을 부팅 로그에 남긴다. T-07·T-08 결과에 우선순위 구성을 조건으로 기록한다. 우선순위 비교(기본값 대 명시)를 cfg 플래그로 고를 수 있게 하면 G3 근거가 된다. |
| M-04 | `components/tfw_estop_io/tfw_estop_io.c:122-143`, `main/tfw_main.c:134,286`, `components/tfw_logic/tfw_regwin.c:190-194`, `components/tfw_modbus/tfw_modbus.c:156` | 비상정지는 **엣지에서만** 래치된다. 부팅 시 GPIO19가 이미 활성이면(코드도 이를 "ACTIVE (already tripped?)"로 로그만 남김) `latched=0`이라 RUN 명령과 레거시 START가 허용된다. 기본 경로는 `TFW_CFG_BRAKE_USE`가 꺼져 있으면 A만(`tfw_main.c:286`)이므로 하드웨어 브레이크도 걸리지 않는다. 결과: **비상정지 입력이 활성인 채로 펄스가 나간다**. TFW-04 완료 기준 #2(래치 중 이동 거부)의 취지와 맞지 않는다. | init에서 입력이 활성이면 즉시 `tfw_es_trip`(래치)한다. `tfw_cmd_check`와 레거시 START 조건에 "입력 활성"도 넣는다(`latched || input_active`). 호스트 시험에 "부팅 시 활성 → 거부" 경우를 추가한다. |
| M-05 | `components/tfw_pulse2/tfw_pulse2.c:186-198,286-317` | servo1axis는 타이머 주기를 즉시 갱신(`update_period_on_empty` 없음, `motion_core.c:71-77`)했는데, 시험 펌웨어는 주기·비교값 모두 TEZ 갱신으로 바꿨다. 정지 상태에서 `set_ticks()`로 쓴 새 주기가 시작 직후 첫 주기에 적용되는지는 미확인이다. 적용되지 않으면 첫 펄스가 직전 운전의 마지막 주기(예: 1 MHz 고정 운전을 STOP_ALL한 다음 5 kHz 시작)나 생성 시 주기(res/1000)로 나간다. 이는 "요청 초과 출력 0"(T-09), 시작 속도 규칙과 충돌할 수 있고, 기존 코드에서 동작이 바뀐 부분이라 D-028 실적으로 보증되지 않는다. | HIL 첫 항목으로 "정지 → 다른 주파수로 시작" 직후 첫 2~3주기를 LA로 확인하는 시험을 시나리오에 넣는다. 문제가 있으면 정지 상태에서는 즉시 갱신을 쓰고(예: 시작 전에 타이머를 STOP_EMPTY 상태에서 한 주기 돌려 TEZ를 거치게 하는 방법 등) 운전 중에만 TEZ 갱신을 쓴다. |
| M-06 | `components/tfw_pulse2/tfw_pulse2.c:138,148-156`, `main/tfw_main.c:206-211` | TASK_10MS 모드에서도 지연 판정 기준으로 `s_upd_us`(=`cfg_upd_us`, 기본 1,000 µs, `tfw_regwin.c:44`)를 쓴다. 실제 주기는 10 ms이므로 `interval > 1.5 × 1000 µs`가 매번 참이라 **모든 호출이 지연으로 세어진다**. TFW-02 완료 기준 #4(TASK_10MS 대 HWTIMER 갱신 지연 비교표)와 T-05 "갱신 지연 초과 0" 판정이 TASK 모드에서는 무의미해진다. | TASK 모드에서는 기준 주기를 10,000 µs로 고정한다(또는 `update_task`의 실제 주기를 `s_upd_us`로 맞춘다). 모드와 기준 주기를 부팅 로그와 결과에 남긴다. |
| M-07 | `host/tfw_host/load.py:66-69`, `host/tfw_host/scenario.py:98,104-107`, `components/tfw_logic/tfw_seq.c:14-15` | MIX/FC16 부하가 시험 창 `SEED`(0x700A)에 계속 쓴다. 시나리오는 부하를 **RUN 명령 전에** 시작하므로, `tfw_seq_run`이 시작 시 읽는 `w->seed`는 시나리오의 seed(예: T-05 777)가 아니라 부하가 마지막으로 쓴 값이다. T-05 시퀀스를 재현할 수 없고, 같은 seed로 T-04와 비교하는 근거(T-05 "T-04 반복 중")가 사라진다. C-02의 호스트 재현 방식도 쓸 수 없게 된다. | 부하 쓰기 대상을 시험 결과에 영향이 없는 레지스터로 바꾼다(전용 더미 레지스터를 추가하거나 레거시 HR 중 영향 없는 칸). 또는 부하를 RUN 후에 시작하고 seed를 다시 읽어 결과 JSON에 기록한다. |
| M-08 | `main/Kconfig.projbuild:12-20,37-39`, `components/tfw_pulse2/tfw_pulse2.c:181-184,409` | 축2 핀 기본값 GPIO2/3/4는 근거 없는 임시값(notes §7-6)인데, 기본 빌드(`TFW_AXIS_COUNT=2`)가 이 핀들을 바로 출력으로 구동한다. `TFW_PROBE_GPIO` 기본값 2가 축2 PLS 기본값 2와 겹쳐, 부팅 probe가 축2 펄스선에 MCPWM 제너레이터·PCNT를 붙였다 뗀다. EMAC/RMII 핀, GPIO51(PHY 리셋), 레거시 서보 출력 23/26/27/46, GPIO19, 축1 핀과 겹치는지 검사하는 코드도 없다. 보드 내부 용도는 미확인(PS-27 전)이다. | 축2 핀 기본값을 -1로 두고 -1이면 축2를 만들지 않게 한다(0x12 경로로 보고). 사용자가 반드시 지정하게 한다. 부팅 시 핀 중복 검사(축1·축2·비상정지·디버그·probe·레거시 출력·GPIO51)를 넣어 겹치면 해당 축을 만들지 않는다. probe 핀 기본값도 -1(probe 생략)로 둔다. |
| M-09 | `host/tfw_host/judge.py:61-66`, `host/tfw_host/scenario.py:58-73` | T-07의 `relaunch_after_latch`는 어디에서도 측정·기록되지 않는다(펌웨어 출력에도 시나리오에도 없음). 그런데도 보고서 사유에 "래치 중 재기동 0"이 사실처럼 적힌다. architecture §6.1 T-07 기준 "래치 중 재기동 0"을 측정 없이 충족한 것처럼 보고한다. | 측정 수단(예: 래치 중 RUN 시도 → status 0x10 확인, 해제 후 일정 시간 PCNT 증가 0 확인)을 시나리오에 넣고, 값이 없으면 "미측정"으로 보고한다. |

### Minor

| ID | 파일:줄 | 근거 | 권고 |
|---|---|---|---|
| m-01 | `components/tfw_pulse2/tfw_pulse2.c:119-121` | `past * cur_ticks * s_ns_per_tick`는 32비트 곱이다. 2 MHz·65,535틱이면 펄스당 32.8 ms라 past ≥ 132에서 넘친다(저속 구간에서 지연 추정치가 작게 나와 거짓 양호로 보일 수 있다). | 포화 곱셈 또는 `past`와 `cur_ticks`를 따로 저장한다. |
| m-02 | `components/tfw_pulse2/tfw_pulse2.c:230`, `main/tfw_main.c:225-227` | 갱신 시간 환산은 `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ`를 쓰고, 부팅 로그·0x7037은 clk_tree 실측값을 쓴다. 둘이 다르면(칩 리비전별 클록 차이 등) `upd_max_us`·`upd_late`가 틀린다. | 측정한 MHz를 `tfw_pulse2_init`에 넘긴다. |
| m-03 | `components/tfw_modbus/tfw_modbus.c:225-226`, `main/tfw_main.c:132-140`, `components/tfw_pulse2/tfw_pulse2.c:288-314` | `on_effect`/`tfw_handle_cmd`는 `s_lock` 밖에서 여러 연결 태스크가 동시에 부를 수 있어 `s_seq_running` 검사와 설정 사이에 경쟁이 있다. 레거시 START(우선순위 7)와 시퀀서(8)가 축1 `start_common`의 `active` 검사·설정 사이에서 경쟁할 수 있다(T-05에서 실제 HMI를 붙이면 생길 수 있다). | 명령 처리를 뮤텍스로 직렬화한다. 시퀀서 실행 중에는 레거시 START를 거부한다. |
| m-04 | `components/tfw_pulse2/tfw_pulse2.c:440,454` | probe의 2번째 제너레이터(`gen_gpio_num=-1`)와 2번째 PCNT 채널(`edge_gpio_num=-1`)은 인자 검사에서 실패할 수 있다. 그러면 자원 부족과 구분되지 않는다(notes §6이 이미 언급). | 실제 다른 GPIO 하나를 probe 전용으로 쓰거나, `ESP_ERR_INVALID_ARG`이면 "인자 문제, 판정 불가"로 따로 로그를 남긴다. |
| m-05 | `host/tfw_host/judge.py:29-31`, `tests/test-fw/test_scenario_report.py:93` | 측정 허용오차 0.5% [가정] 때문에 199.5 kHz가 T-03 PASS가 된다. NFR-002 기준은 ≥ 200 kHz다. 허용오차의 근거(LA 샘플링 분해능)가 결과에 남지 않는다. | 허용오차를 LA 샘플링 속도에서 계산해 기록하거나, 10 s 엣지 수 기반 평균 주파수로 ≥ 200 kHz를 판정하고 최저 주기는 기록만 한다. |
| m-06 | `host/tfw_host/scenario.py:141-145`, `host/tfw_host/la.py:73` | `latency()`의 트리거 엣지가 'fall'로 고정돼 있다. DC-17 비교용 active-HIGH 빌드에서는 반대 엣지(해제)를 기준으로 재게 된다. | 시나리오에 `la_trigger_edge`를 두고 장치 극성(부팅 로그)과 맞는지 확인한다. |
| m-07 | `components/tfw_logic/include/tfw_logic.h:54,57` | ESP-IDF 기본 플래그(-fmath-errno)에서 `__builtin_sqrtf`는 음수 입력 대비 `sqrtf` 라이브러리 호출(플래시)을 함께 만들 수 있다. 실제 입력은 음수가 아니어서 호출되지 않을 가능성이 크지만 IRAM 점검에 걸린다. M-02 처리 결과에 따라 불필요해질 수 있다. | tfw_logic에 `-fno-math-errno`를 주거나 정수 연산으로 바꾼다. |
| m-08 | `components/tfw_estop_io/tfw_estop_io.c:74-92` | 경로 A만 선택해도 OST 고장 브레이크는 오퍼레이터에 연결된 채이고 동작만 KEEP으로 바뀐다. OST 브레이크 상태에서 KEEP이 출력에 어떤 영향을 주는지는 미확인이라 경로 A 단독 측정이 섞일 수 있다. | HIL에서 경로 A 단독일 때 브레이크 영향이 없는지 LA로 먼저 확인한다. 영향이 있으면 경로 A 시험은 fault를 바인딩하지 않은 빌드나 설정으로 한다. |
| m-09 | `main/tfw_main.c:95-101,180-181` | `clear_stats`(Modbus 태스크)가 `nvs_load_task`가 쓰는 `g_res.nvs_writes`를 같이 쓴다. TFW-01 §4 "잠금 없는 단일 작성자"를 어긴다(값이 잠깐 틀리는 정도). | CLEAR는 요청 플래그만 세우고 각 작성자가 스스로 지운다. |
| m-10 | `components/tfw_pulse2/tfw_pulse2.c:406-469`, `components/tfw_estop_io/tfw_estop_io.c:171` | probe_caps 결과가 시험 창에 없고(TFW-02 §3 "④에서 주소 배정"), `es_isr_lat_ns`는 항상 0xFFFFFFFF다(notes §5-4). 설계 이탈은 notes에 기록돼 있다. | architect가 주소 배정과 "외부 측정만 인정" 여부를 정한다(아래 §3). |

---

## 2. 중점 검토 사항별 결과

### (1) ESP-IDF 5.5 API 사용 — servo1axis 대조
- **servo1axis와 같은 부분(실적 있음)**: MCPWM 타이머·오퍼레이터·비교기(`update_cmp_on_tez`)·제너레이터 액션, `set_force_level`, `timer_start_stop`, PCNT 유닛(`accum_count=false`, high 30,000, low −1)·채널·엣지 동작·감시점 추가·clear·start 순서, 출력 핀과 PCNT 입력에 같은 GPIO25 사용(`app_main.c:64-67`), IP101 EMAC 초기화(`movion_modbus_tcp.c:919-959`와 사실상 같음). 그룹 번호만 축 번호로 바뀌었다.
- **servo1axis에 없는 API(실적 없음, 헤더 대조 안 됨)**: `mcpwm_timer_config_t.flags.update_period_on_empty`(동작 변화는 M-05), GPTimer(`gptimer_config_t`/`gptimer_alarm_config_t`/콜백 시그니처), MCPWM 고장·브레이크(`mcpwm_gpio_fault_config_t`, `mcpwm_brake_config_t`, `MCPWM_GEN_BRAKE_EVENT_ACTION`, `mcpwm_operator_recover_from_fault`), `gpio_install_isr_service(ESP_INTR_FLAG_IRAM)`, `esp_clk_tree_src_get_freq_hz`. 검토자가 아는 IDF 5.x 이름·필드와는 맞지만 5.5.3 헤더로 확인하지 못했다. 판단은 **미확인**이다. notes §6 목록이 적절하다.
- ISR에서 부르는 드라이버 함수(`mcpwm_timer_set_period`, `mcpwm_comparator_set_compare_value`, `mcpwm_timer_start_stop`, `pcnt_unit_get_count`)가 ISR에서 허용되는지, `*_CTRL_FUNC_IN_IRAM`으로 IRAM에 놓이는지도 미확인이다. notes §4 절차 3과 `check_iram.sh` 결과로 확인해야 한다(M-01 목록 보완 포함).

### (2) ISR 안전성·2축 동시 펄스 누락
- 좋은 점: ISR에서 RTOS API를 부르지 않는다. `completed` 이중 읽기와 단조 보호(`axis_total`)로 30,000 경계에서 생기는 일시값을 막는다. `on_reach`가 force-level을 가장 먼저 수행한다. 축 사이에 상태를 공유하지 않는다.
- 위험: M-01(플래시 함수 호출), M-02(ISR 안 FPU), M-03(같은 코어·같은 레벨이라 감시점 ISR이 늦어짐 → 초과 펄스). 펄스 "누락"은 PCNT가 출력 핀 자신을 세는 구조라 펌웨어 내부 카운터로는 원리적으로 잡히지 않는다. 외부 계수가 필수다(C-02).

### (3) 비상정지(GPIO19) 경로
- 경로 A: ISR 진입 → 두 축 계수 스냅샷(설계 의사코드 순서) → 두 축 force-low·정지. 디바운스 없음은 설계와 맞다.
- 문제: 우선순위 보장이 없다(M-03). 플래시 함수 호출로 패닉 위험이 있다(M-01). 부팅 시 활성 입력이 래치되지 않는다(M-04). 기본 경로는 A만이라 하드웨어 브레이크(B)는 cfg b2를 켜야 쓰인다(`tfw_main.c:286`). 응답 시간은 내부에서 재지 않고(m-10) LA 외부 측정에 의존한다. 디버그 GPIO 기본값이 -1이라 사용자가 설정해야 한다.

### (4) 거짓 합격 위험
- C-01, C-02, C-03, M-09가 해당한다. 보고서 집계(`report.py:46-48`)는 여러 실행 중 가장 나쁜 판정을 쓰므로 문제가 없다. 다만 개별 판정이 PASS 쪽으로 기울어 있어 집계로는 보완되지 않는다.

### (5) coder가 남긴 설계 모순에 대한 검토 의견
1. **T-05 접속 수(6 대 4)**: 모순이 맞다. architecture §6.1 T-05(부하 4 + HMI 1)에 제어 연결 1을 더하면 6이다. TFW-03 §4·완료 기준 #4는 최대 4이고 5번째를 거부한다. interface-spec §5.3 `max_clients` 기본 4 [가정]은 HMI 포함 여부가 정의되지 않았다. coder의 Kconfig 우회(T-05는 8로 빌드, 거부 확인은 4로 빌드)는 시험 펌웨어로서 수용할 수 있다. 다만 지금 시나리오·판정으로는 기본 빌드에서 T-05가 거짓 합격한다(C-03). **결정 후보(사용자 확정 필요)**: (a) 제품 max_clients에 HMI·제어 연결을 포함하고 기본값을 6 이상으로 올린다, (b) 4를 유지하고 T-05 조건을 "부하 2 + HMI 1 + 제어 1"로 바꾼다. architect 검토가 필요하다.
2. **0x7040 영역 방향**: 모순이 맞다(TFW-01 §3의 0x7004 행 "축별은 0x7040~"와 출력 행 0x7040~ "축별 마지막 목표·PCNT"). coder가 출력 전용으로 해석한 것은 더 구체적인 출력 행과 맞고, T-04는 무작위 거리라 축별 목표 입력이 필요 없으므로 타당하다. architect가 0x7004 행의 문구를 지우거나, 축별 목표 입력 주소를 따로 배정해야 한다. C-02 권고(축별 누적 목표 합 출력)를 할 때 이 영역에 함께 배정하는 것을 제안한다.
3. **축2 핀 임시값**: 임시값 자체는 PS-27 전이라 피할 수 없다. 그러나 기본값으로 바로 구동하고 probe 핀과 겹치는 것은 코드 결함이다(M-08). 기본 -1과 사용자 지정 강제를 권고한다.

## 3. 이슈 제기 (다른 단계·사용자 판단 필요)
1. (architect) T-05 접속 수와 제품 `max_clients`의 정의(HMI 포함 여부) — §2(5)-1의 결정 후보.
2. (architect) TFW-01 §3 0x7004 행 "축별은 0x7040~" 문구 정정, probe_caps 결과·축별 누적 목표 합의 시험 창 주소 배정.
3. (architect) ISR 우선순위 배치(비상정지 > 감시점 > 갱신 > 통신)를 TFW-02/04 제약에 명시할지. T-07·T-08 결과 해석의 전제가 된다.
4. (architect/사용자) ESP32-P4 ISR 안 FPU 사용 가부(M-02)는 MOD-07 제품 설계의 갱신 연산 형식 결정과 이어진다(notes 이슈 5).
5. (사용자) HIL 첫 빌드 때 notes §6 API 목록, `check_iram.sh` 결과, M-05 첫 주기 파형을 먼저 확인해 coder에게 전달해 주기를 요청한다.

## 4. 재검토 조건
- C-01~C-03은 수정과 pytest 음성 시험 추가가 확인되어야 한다.
- M-01, M-04, M-06, M-07, M-08은 코드 수정이 확인되어야 한다.
- M-02, M-03, M-05는 수정하거나 HIL 확인 절차를 notes에 명시해야 한다.
- 위 조건을 갖추면 2차 검토(`test-fw-review-2.md`)에서 조건부 통과 이상을 판단한다.
