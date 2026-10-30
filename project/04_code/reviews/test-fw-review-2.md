# 코드 검토: 시험 펌웨어 TFW-01~05 (D-025 예외, G3 전 실측용) — 2차

| 항목 | 내용 |
|---|---|
| 검토 대상 | `project/04_code/test-fw/`(펌웨어·호스트 도구), `project/04_code/tests/test-fw/`, `project/04_code/notes/test-fw.md` §8(지적별 반영 표)·설계 차이 #14~16 |
| 기준 | 1차 검토 `reviews/test-fw-review-1.md`, `03_design/modules/TFW-01~05`(v0.1), `03_design/architecture.md` §6.1 T-03~T-09 |
| 검토자 | code-reviewer, 2026-10-30 |
| 테스트 결과 | 검토자는 실행하지 않았다. notes §3 "2회차 결과"(C 38개 함수, checks=16583 failed=0; pytest 212 passed)와 메인 세션 재실행 결과(checks=16583 failed=0, 212 passed)를 인용만 한다. ESP-IDF 빌드·HIL은 여전히 한 번도 실행되지 않았다(notes §8 머리말). |

## 판정: **재작업**

- 남은 지적: **Critical 1, Major 5, Minor 8** (1차 지적 중 미해결·부분 해결 포함, 신규 포함).
- 1차 C-01·C-03의 판정 전제조건(완료 상태·seq_done·파라미터 읽기 대조·접속 거부·NVS 쓰기 수·장치 iram 플래그)은 코드와 음성 시험으로 확인했다. 거부·중단·타임아웃·키 누락에서 PASS가 나는 경로는 더 이상 없다.
- 재작업 사유는 두 가지다.
  1. **N-04 (Critical)**: 로직 분석기 CSV를 실행 직후 같은 프로세스에서 바로 읽는다. 그래서 판정에 쓰이는 외부 계수는 구조적으로 **이전 캡처**의 값이다. C-02 수정이 외부 계수에 기대고 있으므로, T-04/05/06의 "외부 계수 = 기대 목표 합" PASS는 지금 상태로는 그 실행을 증명하지 못한다.
  2. **N-01 (Major)**: 기본 빌드가 컴파일되지 않는다. 비활성 bool Kconfig 매크로를 C 식 안에서 쓰고 있기 때문이다. M-02 대체 경로(TASK 모드)와 T-06 비교 빌드(iram_off)도 같은 이유로 컴파일되지 않는다.
- 추가로 비상정지 경로에서 신규 Major 2건이 있다. N-02는 초기화가 실패하면 차단 없이 운전이 허용되는 문제, N-03은 시작 도중이나 반복 사이에 트립이 오면 래치 후에도 펄스가 나가고 내부 계수가 가려지는 문제다.

---

## 1. 1차 지적별 확인

| ID | 1차 심각도 | 2차 결과 | 확인 근거(파일:줄) | 비고 |
|---|---|---|---|---|
| C-01 | Critical | **해결** | `host/tfw_host/judge.py:37-64`(`_precheck`: 키 누락→NOT_RUN, 0x11·값 불일치·CLEAR 실패·SEED 변경·타임아웃·DONE 아님·seq_done 부족→FAIL), `:102,120,126,165,199,205`(T-03~T-09 모두 적용), `scenario.py:133-138`(STOP_ALL→CLEAR 확인), `:96-108`(입력 전부 되읽기), `:157`(wait_status/timed_out 기록). 펌웨어는 RUN 수락 시 status=RUNNING을 응답 전에 동기로 설정(`main/tfw_main.c:143`, `tfw_modbus.c:225-234`). 그래서 `_wait_done`이 묵은 상태로 일찍 끝나도 seq_done 검사로 FAIL이 된다. | 음성 시험 `tests/test-fw/test_judge_regress.py:42-87`(5개 종료 상태 × 6개 T-ID, 키 13개 누락), `:131-153`(모의 장치 종단). 단, 외부 계수의 "이 실행" 대응은 N-04 참조 |
| C-02 | Critical | **부분 해결 → N-04로 Critical 유지** | 외부 계수 없음 → NOT_RUN(`judge.py:93-94`), 축별 기대값(`judge.py:73-78`, `scenario.py:192-197`), 호스트 xorshift 재현(`expect.py:7-41`)이 `tfw_core.c:50-72`·`tfw_seq.c:24-31`의 소비 순서(거리 1회 + 속도·가속·감속 3회)와 일치함을 확인했다. | 판정 로직 자체는 맞다. 그러나 CSV가 그 실행의 캡처라는 보장이 없다(N-04) |
| C-03 | Critical | **부분 해결 → 잔여 R-01(Major)** | T-05: 거부 0·요청 > 0·실행 중 최대 접속 ≥ 1+부하+HMI·빌드 max_clients ≥ 필요 수(`judge.py:139-154`). accept 후 close를 거부로 계산(`load.py:72-80`). T-06: `nvs_writes ≥ 기대치`(`judge.py:176-178`), iram 플래그는 장치 b0에서 읽음(`judge.py:179-182`, `tfw_main.c:21-25,237`) | 부하가 운전 내내 **유지**됐는지는 보지 않는다(R-01) |
| M-01 | Major | **해결(빌드 후 확인 남음)** | `tfw_es_trip`이 `always_inline`으로 옮겨짐(`tfw_logic.h:12,175-181`). `tfw_core.c`에 사본 없음. ISR 안 `esp_timer_get_time` 제거(`tfw_estop_io.c:45-59`). ISR 호출 집합 = count_isr·stop·trip·cycle_count·DBG. IRAM 플래그는 IRAM 안전 빌드에서만(`tfw_estop_io.c:139-141`) | 잔여 Minor n-3(`check_iram.sh` 목록에 `mcpwm_timer_start_stop` 없음). 이 파일은 iram_off 빌드에서 컴파일 오류가 난다(N-01, `:156`) |
| M-02 | Major | **조건부 수용(HIL 이관)** | 코드 미변경(`tfw_logic.h:50-62`). notes §8 M-02 | 아래 §3-1. 대체 경로(TASK 모드)가 컴파일되지 않으므로(N-01, `tfw_main.c:258`) 지금은 절차를 실행할 수 없다. N-01을 고쳐야 수용이 성립한다 |
| M-03 | Major | **해결(설정화, P4 효과 미확인)** | Kconfig 3/2/1(`Kconfig.projbuild:40-51`), PCNT `intr_priority`(`tfw_pulse2.c:209-210`), GPTimer(`:278-279`), GPIO 서비스 레벨(`tfw_estop_io.c:137-142`), 부팅 로그(`tfw_pulse2.c:242-244`, `tfw_estop_io.c:155`) | 레벨 지정으로 인터럽트 할당 실패 경로가 새로 생겼다(N-02). 이미 설치된 서비스이면 경고만 남긴다(`:143`) |
| M-04 | Major | **해결** | 부팅 시 활성 → 래치(`tfw_core.c:83-86`, `tfw_estop_io.c:153`). `tfw_estop_blocking`을 명령 검사(`tfw_main.c:139`), 시퀀서 abort(`:118`), 레거시 START(`tfw_modbus.c:156`)에 사용 | 초기화가 실패하면 래치·ISR 자체가 없는 문제는 N-02 |
| M-05 | Major | **미해결(HIL 절차 불완전)** | `scenario_example.yaml:6-8` | 아래 §3-2. 1 MHz 선행 단계가 없고, 요청보다 낮은 첫 주기는 검출하지 못하며, 묵은 CSV 문제(N-04)도 있다 |
| M-06 | Major | **해결** | `tfw_logic.h:98`, `tfw_pulse2.c:230,138`. 부팅 로그 `:242-244` | 결과 레지스터에 모드 항목이 없는 것은 이슈 제기로 넘어감(수용) |
| M-07 | Major | **해결** | 부하 FC16이 시작 시 읽은 SEED 값을 되쓰기만 함(`load.py:65-67,86`), 실행 후 SEED 재확인(`scenario.py:159`, `judge.py:50-51`) | 잔여 Minor n-2(첫 읽기 타임아웃이면 0을 씀) |
| M-08 | Major | **해결** | 축2·probe 기본값 -1(`Kconfig.projbuild:12-20,37-39`). 중복 검사(`tfw_pulse2.c:248-275,462-469`, `tfw_core.c:112-120`). 음수 항목은 비교에서 빠진다 | EMAC/RMII 핀 미포함은 미확인 사항으로 notes에 명시됨(수용) |
| M-09 | Major | **해결** | `scenario.py:111-124`, `judge.py:192-197`(미측정이면 RECORD에 "미측정(합격 근거 아님)", 측정돼도 PASS가 아니라 RECORD) | — |
| m-01 | Minor | 해결 | `tfw_pulse2.c:120`, `tfw_logic.h:92-96` | — |
| m-02 | Minor | 해결 | `tfw_main.c:243-250` | — |
| m-03 | Minor | 부분 해결 | 명령 직렬화(`tfw_main.c:157-174`) | 레거시 START는 `seq_busy` 검사 후 시작한다(`tfw_modbus.c:156-158`). 검사와 시작 사이 경쟁이 남는다(n-5) |
| m-04 | Minor | 해결 | `tfw_pulse2.c:505-507` | — |
| m-05 | Minor | 해결(T-03) | `judge.py:105-117` | 같은 문제가 T-09 허용오차에 남아 있다(n-1) |
| m-06 | Minor | 부분 해결 | `scenario.py:200-203` | 극성을 장치가 아니라 시나리오 두 값끼리 비교한다(n-4) |
| m-07 | Minor | 반영 주장(빌드 후 확인) | notes §8. `test_firmware_static.py:101-102` | 효과는 `check_iram.sh`로 확인해야 한다 |
| m-08 | Minor | 문서(HIL) | notes §8 | 수용 |
| m-09 | Minor | 해결 | `tfw_main.c:102-103,188` | 같은 유형이 갱신 통계에 남아 있다(n-8) |
| m-10 | Minor | 이슈 제기 | notes §8 이슈 3 | 수용(architect 판단) |

---

## 2. 남은 지적 및 신규 지적

### Critical

| ID | 파일:줄 | 근거 | 권고 |
|---|---|---|---|
| N-04 (C-02 잔존) | `host/tfw_host/scenario.py:226-227`, `:179-190`, `host/tfw_host/cli.py:21-23`, `tests/test-fw/test_judge_regress.py:109-113` | `run_scenario`는 `run_step`(장치 운전)이 끝나는 즉시 같은 루프에서 `analyse_la`를 불러 `la_csv` 파일을 읽는다. 사용자가 이번 실행의 캡처를 내보낼 시점이 없으므로, 읽히는 파일은 (a) 없거나(→ OSError → FAIL) (b) **앞선 실행의 캡처**다. 재판정 명령도 없다(`cli.py`에는 run/report만 있음). 같은 seed면 기대 엣지 수가 같으므로, 이번 실행에서 펄스가 빠져도 앞선 실행의 정상 캡처로 T-04/05/06이 **PASS**가 된다. PCNT는 출력 핀 자체를 세므로(1차 C-02) 누락의 유일한 증거가 외부 계수다. 결국 C-02가 막으려던 거짓 합격이 다른 경로로 남는다. 회귀 시험도 CSV를 실행 **전에** 만들어 두는 순서로 작성돼 이 흐름을 정상으로 굳힌다. T-03, T-09, M-05 첫 주기 확인도 같은 구조다. | 실행과 판정을 나눈다. `run`은 판정을 PENDING으로 두고 실행 시작·종료 시각과 run_s를 JSON에 남긴다. 새 `judge <results> <step> <csv>` 명령이 나중에 CSV를 붙인다. 판정할 때는 다음을 전제로 둔다. (1) CSV 수정 시각 > 실행 종료 시각, (2) 캡처 길이 ≥ run_s(첫 엣지 이전·마지막 엣지 이후 여유 포함), (3) CSV sha256을 결과에 기록하고 같은 해시가 두 실행에 쓰이면 FAIL. 대화형이 필요하면 run_step 뒤에 "캡처를 내보낸 뒤 Enter"로 멈추고 같은 검사를 한다. 음성 시험으로 "실행 전에 만든 CSV → PASS 아님"과 "같은 CSV 재사용 → FAIL"을 추가한다. |

### Major

| ID | 파일:줄 | 근거 | 권고 |
|---|---|---|---|
| N-01 (신규) | `main/tfw_main.c:292`, `:258`, `:290`, `:306`, `components/tfw_estop_io/tfw_estop_io.c:155-156`, `main/Kconfig.projbuild:28-30,52-57` | ESP-IDF의 `sdkconfig.h`는 값이 n인 bool 옵션을 **정의하지 않는다**. 그런데 다음 매크로를 `#if`가 아니라 C 식 안에서 쓴다. `CONFIG_TFW_LEGACY_NVS_ON_WRITE`(기본 n → **기본 빌드가 컴파일 오류**, `tfw_main.c:292`), `CONFIG_TFW_UPD_MODE_HWTIMER`(n이면 오류 → M-02 대체 경로를 빌드할 수 없음, `:258`), `CONFIG_TFW_ESTOP_ACTIVE_LOW`(n이면 오류 → DC-17 active-HIGH 비교 빌드 불가, `:290,306`), `CONFIG_PCNT_ISR_IRAM_SAFE`(`sdkconfig.tfw_iram_off`에서 미정의 → **T-06 비교 빌드 불가**, `tfw_estop_io.c:156`). 같은 파일의 다른 곳은 `#if`로 올바르게 썼다(`tfw_main.c:21,224,261-285`, `tfw_estop_io.c:139`). notes §0의 스텁 문법 검사는 스텁이 모든 매크로를 정의해 이를 잡지 못한 것으로 보인다(스텁 내용은 미확인). | 파일마다 `#if CONFIG_X` → `#define TFW_X 1 / #else 0`인 보조 매크로를 두고 C 식에서는 보조 매크로만 쓴다. 스텁 문법 검사를 bool 옵션 조합 4가지(기본, HWTIMER=n, ACTIVE_LOW=n, iram_off)로 각각 돌린다(정의하지 않은 매크로로 `-fsyntax-only`). `test_firmware_static.py`에 "bool CONFIG_를 `#if` 밖 C 식에서 쓰지 않음" 검사를 추가한다. |
| N-02 (신규) | `components/tfw_estop_io/tfw_estop_io.c:132-146`, `:153`, `:162`, `main/tfw_main.c:306-307`, `components/tfw_modbus/tfw_modbus.c:131-148` | `tfw_estop_init`이 `gpio_config`, `gpio_install_isr_service`(M-03에서 `ESP_INTR_FLAG_LEVEL3`를 추가하면서 할당 실패 가능성이 새로 생김), `gpio_isr_handler_add` 중 하나에서 실패하면 바로 반환한다. 그러면 ISR 경로 A, 부팅 래치(M-04, `:153`), `es_task`(`:162`)가 모두 없다. `app_main`은 오류 로그만 남기고 Modbus를 시작하므로 RUN과 레거시 START가 허용된다. 시퀀서는 1 ms 폴링(`tfw_main.c:118`)으로만 멈추고, 레거시 운전 루프는 이동 중 비상정지를 확인하지 않는다(`tfw_modbus.c:131-148`). 이 상태에서 T-07을 재면 폴링 경로의 응답이 "경로 A(ISR)" 결과로 기록된다. 서비스가 이미 설치돼 레벨·IRAM 플래그가 적용되지 않은 경우(`:143`)도 로그로만 남아 결과의 전제(M-03)가 기록되지 않는다. | 초기화가 실패하면 영구 차단 플래그를 세운다. `tfw_estop_blocking()`이 true를 반환해 RUN을 0x12 또는 0x10으로 거부하게 하고, `es_last_err`·`es_bind_ok`에 표시한다. "서비스 기존 설치(플래그 미적용)"도 `es_last_err` 값으로 내보낸다. T-07 판정에 `es_last_err == 0`과 `es_trip_count` 증가를 전제로 넣는다(아니면 NOT_RUN). |
| N-03 (신규) | `components/tfw_pulse2/tfw_pulse2.c:344-348` 대 `:376-383`·`:114`, `components/tfw_logic/tfw_seq.c:20-53`, `main/tfw_main.c:109-112`, `tfw_pulse2.c:335` 대 `tfw_estop_io.c:66-67` | (a) 비상정지 ISR이 `start_common`의 `active = true`(`:345`)와 타이머 시작(`:348`) 사이에 들어오면 다음 순서가 된다. ISR이 force-low·정지한 뒤 태스크가 force를 풀고(`:346`) 타이머를 다시 시작한다. 이때 `active == false`라서 감시점 ISR도 목표에서 멈추지 않는다(`:114`). 배치 A에서는 시퀀서 태스크와 비상정지 ISR이 같은 코어 0에 있어 선점으로, 배치 B에서는 다른 코어에서 병렬로 생긴다. (b) 시퀀서는 `aborted()`를 대기 루프 안, 그것도 "busy인 축이 있을 때"만 확인한다(`tfw_seq.c:48-50`). `hw_move`/`hw_fixed`도 래치를 확인하지 않는다(`tfw_main.c:109-112`). 그래서 반복 사이나 (a) 이후에 트립되면 **다음 반복의 두 축이 래치 상태에서 시작**된다. 그 뒤 첫 대기 검사에서야 정지한다. (c) 이렇게 다시 시작하면 `pcnt_unit_clear_count`(`tfw_pulse2.c:335`)가 계수를 지운다. 10 ms 뒤 `es_task`가 계산하는 `es_pulses_after`(`tfw_estop_io.c:67`, 최종 < 스냅샷이면 0)는 트립 후 펄스를 **0으로 가릴 수 있다**. TFW-04 완료 기준 #2(래치 중 이동 거부)와 T-07·DC-08 계수 기록에 어긋난다. | `tfw_seq_run` 각 반복의 시작과 축별 시작 직전에 `hw->aborted()`를 확인한다. `hw_move`/`hw_fixed`에서 `tfw_estop_blocking()`이면 시작을 거부한다. `start_common`은 타이머를 시작한 뒤 래치를 다시 읽고, 래치면 즉시 정지한다. 또는 ISR이 세우는 축별 inhibit 플래그를 `active` 설정과 함께 spinlock(`portENTER_CRITICAL`/`_ISR`)으로 보호한다. `s_pending` 동안에는 PCNT clear를 금지한다. 호스트 C 시험을 추가한다. fake hw에서 반복 사이에 `aborted()`를 true로 만들고 그 뒤 `move` 호출이 0건인지 검사한다. |
| R-01 (C-03 잔여) | `host/tfw_host/judge.py:145-149`, `scenario.py:144-147`, `load.py:97-99` | 실행 중 접속 수는 **최댓값**만 쓴다(`seen = max`). 부하 클라이언트가 운전 도중 끊기면(`load.py:97-99`, 오류 1건 후 루프 종료) 이후 운전은 부하 3개 이하로 진행된다. `errors`·`timeouts`는 판정에 쓰이지 않는다. T-05 "T-04 반복 중 TCP 클라이언트 4개 연속" 조건이 시작 직후 한순간만 확인돼도 PASS가 된다(T-05 운전은 수십 분 이상). | 실행 중 `MB_CLIENTS` 최솟값도 기록하고 `min ≥ want`를 PASS 전제로 둔다(첫 샘플 전 안정화 시간은 제외). 클라이언트별 요청 수 > 0과 "중도 종료 0"을 요구한다. `errors > 0`이면 최소 RECORD 사유로 남기고, 중도 단절은 FAIL로 처리한다. |
| M-05 (잔존) | `host/scenario_example.yaml:6-8`, `scenario.py:208-210` | 1차 지적의 조건은 "직전 운전의 마지막 주기(예: 1 MHz) → 다른 주파수 시작"이다. 그런데 시나리오에는 1 MHz 선행 단계가 없다. 첫 단계는 부팅 직후 생성 시 주기(res/1000 = 1 kHz, `tfw_pulse2.c:191`) 상태에서 5 kHz로 시작하는 경우만 만든다. 이 경우 첫 주기가 이전 값으로 나가도 주파수는 **요청보다 낮으므로** T-09 규칙("요청 초과")으로 검출되지 않는다. "눈으로 확인"만 남는다. CSV도 N-04 구조라 이번 실행의 파형이 아니다. | 선행 단계 `RUN_FIXED 1 MHz`(짧은 count)를 넣고, 바로 다음 5 kHz 단계에서 첫 3주기를 검사하는 판정(`first_periods_us`: 각 주기가 기대 주기 ±LA 분해능 안)을 `la.py`/`judge.py`에 추가한다. 반대 경우(5 kHz → 1 MHz, 첫 주기 길어짐)도 넣는다. N-04 해결 후 CSV를 붙인다. |

### Minor

| ID | 파일:줄 | 근거 | 권고 |
|---|---|---|---|
| n-1 | `host/tfw_host/scenario.py:210`, `scenario_example.yaml:8` | T-09 "요청 초과" 검사 허용오차 기본값이 0.5% [가정]이다. ceil 규칙을 쓰지 않았다면 내림 규칙의 초과량(예: 10 MHz/3 kHz → 3,333틱, 약 0.01% 초과)이 이 허용오차 안에 숨는다. m-05와 같은 유형이다. | 허용오차를 LA 샘플링 분해능에서 계산해 기록하거나, 평균 주기(다수 주기) 기준으로 판정한다. |
| n-2 | `host/tfw_host/load.py:66-71,86` | 첫 SEED 읽기가 타임아웃이면 `wvals = [0, 0]`으로 두고 FC16으로 SEED에 0을 쓴다. 부하는 RUN 전에 시작하므로(`scenario.py:142-150`) 실행 seed가 0(무작위 아님)으로 바뀔 수 있다. 결과는 FAIL로 드러나지만(거짓 합격 아님) M-07의 의도와 어긋난다. | 첫 읽기가 실패한 클라이언트는 FC16을 보내지 않거나, 거부·오류로 처리하고 종료한다. |
| n-3 | `tools/check_iram.sh:10` | 비상정지 ISR이 부르는 `tfw_pulse2_stop`(`tfw_pulse2.c:383`)은 `mcpwm_timer_start_stop`을 호출한다. 이 함수가 `MCPWM_CTRL_FUNC_IN_IRAM`으로 IRAM에 놓이는지는 **미확인**인데 점검 목록에 없다. 호출 대상 역어셈블 루프(`:14-16`)로 간접 확인은 가능하다. | 목록에 `mcpwm_timer_start_stop`, `gpio_set_level`(디버그 핀 사용 시) 결과 판정 기준을 명시한다. IRAM이 아니면 경로 A는 force-level만 쓰고 타이머 정지는 태스크로 미룬다. |
| n-4 | `host/tfw_host/scenario.py:202-203` | m-06 극성 대조가 시나리오의 `la_trigger_edge`와 시나리오의 `estop_active_low`끼리 비교한다. 장치의 빌드 극성은 읽지 않는다(시험 창에 극성 항목 없음). `estop_active_low`를 빼면 검사가 생략된다. | 빌드 극성을 `cfg_flags` 읽기 전용 비트나 별도 레지스터로 내보내 장치 값과 비교한다(주소 배정은 architect). 키가 없으면 T-07을 NOT_RUN으로 둔다. |
| n-5 | `components/tfw_modbus/tfw_modbus.c:156-158`, `main/tfw_main.c:143-144` | 레거시 START는 `seq_busy()`를 확인한 뒤 `tfw_pulse2_move`를 부른다. 시퀀서도 `s_seq_running`만 보고 시작한다. 검사와 시작 사이에 남은 경쟁(TOCTOU) 때문에 두 태스크가 축1 `start_common`의 `active` 검사·설정에 동시에 들어갈 수 있다(m-03 잔여). | 축 시작을 `s_cmd_lock` 또는 축별 뮤텍스 안에서 하거나, `start_common`의 `active` 검사·설정을 원자적으로 만든다. |
| n-6 | `main/tfw_main.c:21-25,237`, `sdkconfig.tfw_iram_on:3-10` | 장치 `cfg_flags` b0는 `CONFIG_PCNT_ISR_IRAM_SAFE` 하나만 반영한다. notes §2-2는 옵션 이름이 틀리면 조용히 무시될 수 있다고 적었다. 그 경우 GPTimer·MCPWM 쪽이 IRAM 안전이 아니어도 T-06 "on(장치 확인)" PASS가 된다. T-06 기준이 "PCNT ISR IRAM 안전 설정"이라 위반은 아니지만 G3 근거로는 약하다. | b0를 필요한 옵션 전부의 AND로 하거나 옵션별 비트를 내보낸다. 보고서에 부팅 로그의 IRAM 옵션 줄(`tfw_main.c:260-286`)을 첨부하도록 한다. |
| n-7 | `host/tfw_host/judge.py:73-78`, `scenario.py:179-190`, `scenario_example.yaml:10,12,14` | T-04/05/06 외부 계수는 LA CSV의 엣지 수만 받는다. 1,000회 무작위 운전(수십 분 [가정: 평균 거리·속도로 추정])을 ≥ 10 MS/s로 연속 캡처하는 것은 일반 LA 메모리로는 어려울 수 있다(장비 사양 미확인). 그러면 결과가 계속 NOT_RUN이 된다. | 외부 카운터(토털라이저) 입력(`ext_edges: [n1, n2]`, 장비명·확인 방법 기록)을 판정 입력으로 허용한다. N-04와 같은 "이 실행" 확인 규칙을 적용한다. |
| n-8 | `components/tfw_pulse2/tfw_pulse2.c:430-434`, `:138` | `tfw_pulse2_clear_stats`(Modbus 태스크)가 GPTimer ISR이 쓰는 `s_upd`를 직접 지운다. m-09와 같은 단일 작성자 위반이며, CLEAR 직후 `late`/`calls`가 한 번 틀릴 수 있다. | CLEAR 요청 플래그를 두고 ISR이 다음 호출에서 지운다. |

---

## 3. HIL 이관(M-02, M-05) 타당성

### 3-1. M-02 (ISR 안 FPU) — **조건부 수용**
- 타당한 점: ESP32-P4 인터럽트 문맥의 FPU 허용 여부는 검토자와 coder 모두 문서로 확인하지 못했다(**미확인**). 정수 연산 전환이 MOD-07 연산 형식 결정(notes 이슈 5)과 묶여 있다는 사유도 맞다. 허용되지 않을 때 가장 가능성 높은 증상은 첫 이동에서 나는 예외·패닉이다. 이는 거짓 합격이 아니라 바로 드러나는 실패다.
- 조건 1: 대체 경로(`CONFIG_TFW_UPD_MODE_HWTIMER=n`)가 **컴파일되지 않는다**(N-01, `tfw_main.c:258`). N-01 수정이 수용의 전제다.
- 조건 2: notes §8의 확인 방법 "1회 이동 시 패닉·레지스터 깨짐 유무"로는 "태스크 FPU 레지스터 손상"(조용한 실패)을 볼 수 없다. HIL 절차에 둘 중 하나를 넣는다. (a) IDF 5.5 문서 "FreeRTOS (IDF) — Floating Point Usage"의 P4 항목을 확인해 결과를 기록하거나, (b) HWTIMER 운전 중 float 검사합 태스크를 돌려 불일치 0을 확인한다.
- 조건 3: notes §4 절차 목록(사용자 실행 순서)에 이 확인을 명시적인 단계로 넣는다. 지금은 §8 표에만 있다.

### 3-2. M-05 (정지 후 첫 주기) — **이관 자체는 타당, 절차는 불충분(Major 유지)**
- 하드웨어 동작(TEZ 갱신이 정지 상태에서 적용되는지)은 호스트에서 확인할 수 없으므로 HIL 이관은 맞다.
- 그러나 지금 절차는 지적한 조건(1 MHz → 5 kHz)을 만들지 않고, 판정 규칙이 낮은 쪽 오류를 검출하지 못하며, CSV가 그 실행의 것이 아니다(§2 M-05, N-04). 권고대로 고친 뒤 수용한다.

---

## 4. 설계에 없는 추가 0x705E(`mb_max_clients`) — **수용**
- 근거: TFW-03 §3 출력 표에서 비어 있는 주소(히스토그램 0x7056~0x705D 다음, TFW-04 영역 0x7060 앞)다. 읽기 전용이고(`tfw_regwin.c:19`, writable 0) 값은 빌드 상수다(`tfw_main.c:94,248`). 기존 항목의 의미와 주소를 바꾸지 않으며, 제품 맵(interface-spec 0x0114 `max_clients`)과는 다른 시험 창 안에 있다. 헤더와 Python 주소표 동기화는 `test_regs_sync.py`로 지켜진다(`tfw_regs.h:50`, `regs.py:15`). 1차 C-03 권고의 직접 이행이며, T-05 설계 모순(6 대 4)이 거짓 합격으로 바뀌지 않게 하는 판정 입력이다(`judge.py:150-152`).
- 조건: 설계서는 수정되지 않았으므로 architect가 TFW-03 §3에 이 항목을 추가해야 한다(아래 이슈 1). 그 전까지 보고서에 "설계 미기재 출력(notes 설계 차이 #14)"이라고 표기한다.

---

## 5. 재검토 조건 (3차)
- N-04: 실행·판정 분리(또는 대화형 정지)와 CSV 시각·길이·해시 검사를 넣고, 음성 시험 2건(실행 전 CSV, CSV 재사용)을 추가한다. 이것이 해결돼야 C-02를 닫는다.
- N-01: 4가지 bool 조합에서 스텁 문법 검사 통과 기록과 정적 시험을 추가한다.
- N-02, N-03, R-01: 코드 수정과 호스트 시험(N-03은 fake hw 시험, R-01은 접속 최솟값 판정 음성 시험)을 추가한다.
- M-05: 시나리오 선행 단계와 첫 주기 판정을 추가한다.
- M-02: §3-1 조건 2·3을 notes에 반영한다.
- 위가 갖춰지면 3차에서 조건부 통과(ESP-IDF 빌드·HIL 미실행 조건)를 판단한다. Minor는 3차 판정을 막지 않는다.

## 6. 이슈 제기 (다른 단계·사용자 판단 필요)
1. (architect) TFW-03 §3에 0x705E `mb_max_clients`(U16, R, 빌드 설정값)를 추가해 달라(§4).
2. (architect) 비상정지 초기화 실패 때의 동작(영구 차단 대 경고)을 TFW-04 제약에 명시할지 정해 달라(N-02). 제품 MOD 설계의 fail-safe 원칙과 이어진다.
3. (architect) 빌드 극성·옵션별 IRAM 플래그를 시험 창에 내보낼 주소를 배정해 달라(n-4, n-6). 1차 이슈 2(probe_caps·모드 주소)와 함께 처리할 것을 제안한다.
4. (사용자) T-04/05/06의 외부 계수 장비를 정해 달라(LA 연속 캡처 대 외부 토털라이저, n-7). 장비 메모리·샘플링 속도는 미확인이다.
5. 1차 이슈 1(T-05 접속 수 6 대 4 결정 후보), 1차 이슈 3·4(ISR 우선순위 명시, P4 ISR FPU)는 여전히 열려 있다.
