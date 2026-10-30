# 코드 검토: 시험 펌웨어 TFW-01~05 (D-025 예외, G3 전 실측용) — 3차(최종)

| 항목 | 내용 |
|---|---|
| 검토 대상 | `project/04_code/test-fw/`(펌웨어·호스트 도구), `project/04_code/tests/test-fw/`, `project/04_code/notes/test-fw.md` §3(3회차 결과)·§4·§9 |
| 기준 | 2차 검토 `reviews/test-fw-review-2.md`, `03_design/modules/TFW-01~05`(TFW-03 v0.2: 0x705E 추가), architecture §6.1 |
| 검토자 | code-reviewer, 2026-10-30 |
| 테스트 결과 | 검토자는 실행하지 않았다. notes §3 "3회차 결과"(C 41개 함수, checks=16591 failed=0; pytest 262 passed)와 메인 세션 확인 결과(checks=16591 failed=0, 262 passed)를 인용만 한다. ESP-IDF 빌드와 HIL은 여전히 한 번도 실행되지 않았다(notes §9 머리말, 남은 위험 1). |

## 판정: **조건부 통과**

- 2차의 Critical 1건(N-04)과 Major 5건(N-01, N-02, N-03, R-01, M-05)은 모두 코드와 호스트 시험으로 해결됐다(§1). M-02 조건 2·3도 notes §4 6b에 반영됐다.
- 남은 Critical·Major는 없다. 이번에 새로 찾은 지적 8건은 모두 Minor다(§2). 각 지적에 대해 거짓 합격이 나지 않는 이유를 근거 열에 적었다.
- 통과 조건은 두 가지다. (1) §3-1의 HIL 확인 항목을 수행한다. (2) G3 근거 실측 동안 §3-2의 운용 조건을 지킨다. 운용 조건은 r3-1을 코드로 고치기 전까지 거짓 합격 경로를 막기 위한 것이다.

### G3 근거 실측에서 거짓 합격이 나는가 — 결론
지금 코드에서는 장치가 실제로 펄스를 잃거나 운전이 실패했는데 판정이 PASS로 나오는 구조적 경로를 찾지 못했다. 근거는 다음과 같다.
- 판정은 완료 상태(DONE), `seq_done`, 파라미터 읽기 대조, CLEAR 결과, SEED 불변을 먼저 확인한다(`judge.py:51-81`).
- T-04/05/06은 외부 계수가 호스트에서 재현한 축별 기대 합과 같아야 PASS다(`judge.py:90-112`). 외부 계수가 없으면 NOT_RUN이다.
- 로직 분석기 데이터는 이번 실행 뒤에 수정된 파일이고, 길이가 실행 시간 이상이고, 해시가 다른 실행에 쓰이지 않았을 때만 판정에 쓰인다(`scenario.py:271-290`, `judge.py:40-48`).
- 비상정지 초기화가 실패하면 운전 자체가 거부된다(`tfw_estop_io.c:204`). 트립 뒤에는 새 이동이 시작되지 않는다(`tfw_seq.c:25,29,44`, `tfw_pulse2.c:326,358-361`, `tfw_main.c:107,109`).
- T-05는 운전 내내 최소 접속 수를 유지해야 한다(`judge.py:170-183`).

다만 신선도 검사는 CSV 수정 시각의 하한과 같은 결과 폴더 안의 해시 비교에 기대고 있다. 그래서 운용자의 실수 하나로 거짓 합격이 날 수 있는 경로가 남는다(r3-1). 예를 들어 캡처를 걸지 않은 채, 결과 폴더를 바꾼 다음 세션에서, 같은 seed 실행에 앞선 캡처를 다시 내보내 붙이면 PASS가 될 수 있다. 실패한 기록을 다른 CSV로 다시 판정해 이력 없이 PASS로 덮어쓰는 경우도 같다. 따라서 §3-2 운용 조건(결과 폴더 하나 유지, `--wait-capture` 사용, FAIL 재판정 금지 또는 재판정 시 사유 기록)을 지키는 한, 이 시험 펌웨어로 G3 근거 실측을 해도 거짓 합격은 나지 않는다고 판단한다. 남은 불확실성은 판정 로직이 아니라 아직 한 번도 빌드·실행되지 않은 펌웨어의 실제 동작에 있다. 이 불확실성은 PASS가 아니라 빌드 오류, FAIL, NOT_RUN 쪽으로 드러난다. 예외 두 가지는 §3-1의 HIL 항목으로 직접 확인해야 한다. 하나는 코어 간 경쟁(N-03 a 잔여)이고, 다른 하나는 IRAM 배치다.

---

## 1. 2차 지적별 확인

| ID | 2차 심각도 | 3차 결과 | 확인 근거(파일:줄) | 비고 |
|---|---|---|---|---|
| N-04 | Critical | **해결** | LA 단계는 실행 후 PENDING으로 남고 `run_start_ts`/`run_end_ts`/`run_s`가 저장된다(`scenario.py:160-170,346-353`). `capture_check`는 수정 시각 > 종료 시각, 길이 ≥ run_s − 0.5 s, 다른 기록에 같은 sha256 없음을 확인한다(`:271-290`). 붙이기·재판정은 `attach_capture`/`judge_file`(`:293-322`), CLI `judge`와 `--wait-capture`는 `cli.py:18-37`. 판정 쪽에서는 LA 키가 있는데 `la_fresh is not True`이면 FAIL이다(`judge.py:40-48`). 이 검사는 `_precheck`(`:74-76`)로 T-03/04/05/06/08/09에, T-07에는 직접(`:219-221`) 적용된다. 보고서는 PENDING을 FAIL보다는 낫고 PASS보다는 못한 상태로 취급한다(`report.py:46-48`). | 음성 시험 `test_round2_regress.py:27-95`(실행 전 CSV → FAIL, 재사용 → FAIL, 짧은 캡처 → FAIL). 잔여 위험은 r3-1 |
| N-01 | Major | **해결(스텁 기준)** | 0/1 보조 매크로를 `tfw_cfg.h:9-31`에 정의했다. C 식에서는 보조 매크로만 쓰고(`tfw_main.c:22,255,287,289,303`, `tfw_estop_io.c:166`), bool `CONFIG_`는 `#if` 안에서만 쓴다(`tfw_main.c:221,258-282`, `tfw_estop_io.c:145`, `tfw_modbus.c:94,227`). `tfw_pulse2.c`에서 쓰는 `CONFIG_`는 모두 int다(Kconfig `:4-49,59-77`). | `test_cfg_syntax.py`(7개 조합 × 2개 파일, notes §9). `tfw_pulse2.c`·`tfw_modbus.c`는 정적 시험으로만 확인했다. 실제 헤더 대조는 HIL 빌드 몫이다(§3-1 #1) |
| N-02 | Major | **해결** | `s_init_ok`는 `estop_init_body`가 모두 성공했을 때만 true가 된다(`tfw_estop_io.c:33,176-187`). `tfw_estop_blocking`은 `!s_init_ok ||`(`:204`)이고, 이 값이 명령 검사(`tfw_main.c:136`), 시퀀서(`:115`), 레거시 START(`tfw_modbus.c:156`), `start_common`(`tfw_pulse2.c:326`)에 쓰인다. ISR 서비스가 이미 설치된 경우는 `es_last_err = INVALID_STATE`로 내보낸다(`tfw_estop_io.c:149-153`). T-07은 `es_last_err == 0`이고 트립 수가 늘어야 한다(`judge.py:222-227`, `scenario.py:149`). | `estop_host/test_estop_host.c`(notes §9, 29건). r3-7 참조 |
| N-03 | Major | **해결(코어 간 경쟁은 HIL)** | (a) ISR이 정지보다 먼저 래치한다(`tfw_estop_io.c:56-64`). `start_common`은 시작 전 차단 검사(`tfw_pulse2.c:326`)와 타이머 시작 후 재검사·정지(`:358-361`)를 한다. (b) 반복 시작(`tfw_seq.c:25`)과 축별 시작 직전(`:29`)에 확인한다. 트립 때문에 시작이 거부되면 오차가 아니라 중단으로 처리한다(`:44`). `hw_move`/`hw_fixed`도 거부한다(`tfw_main.c:107,109`). (c) 차단 중에는 `start_common`이 `pcnt_unit_clear_count`(`:341`)까지 가지 않는다. | `test_logic.c:619-647`, `test_round2_regress.py:180-199`. 잔여 위험은 r3-2, r3-3 |
| R-01 | Major | **해결** | 최솟값(`mb_clients_min`, 안정화 이후 샘플과 마지막 샘플, `scenario.py:147-158,167,171`)이 `want` 이상이어야 한다(`judge.py:170-174`). `dropped`와 `idle_clients`가 0이 아니면 FAIL(`:175-183`, `load.py:105-109,115-116`), `errors`는 기록한다(`judge.py:179-180`). | `test_round2_regress.py:273-306`. 잔여 위험은 r3-5 |
| M-05 | Major | **해결(HIL 확인 남음)** | 시나리오가 1 MHz → 5 kHz → 1 MHz 순서다(`scenario_example.yaml:14-16`). 첫 3주기를 기대 주기 ±2샘플과 비교해 짧은 쪽과 긴 쪽을 모두 FAIL로 판정하고, 주기가 부족하면 NOT_RUN이다(`scenario.py:233-244`, `la.py:108-112`, `judge.py:247-253`). | `test_round2_regress.py:229-264`. r3-6 참조 |
| M-02 조건 2·3 | (조건부 수용) | **해결** | notes §4 6b(57행 부근): 문서 확인 또는 float 검사합 태스크를 사용자 절차에 명시했다. TASK 모드는 N-01로 컴파일 가능해졌다. | HIL(§3-1 #3) |
| n-1 | Minor | 해결 | `scenario.py:224-231`(LA 1샘플 분해능, 근거 기록) | 경계값 비교 r3-8 |
| n-2 | Minor | 해결 | `load.py:69-79`(첫 읽기 타임아웃이면 FC16을 보내지 않음) | — |
| n-3 | Minor | 해결 | `check_iram.sh:10,13-14` | 판단은 첫 빌드 후 |
| n-4 | Minor | 이슈 제기 유지 | notes §9 | architect 주소 배정 대기. 수용 |
| n-5 | Minor | 부분 해결 | `tfw_main.c:107,109`. 축 단위 직렬화는 하지 않았다 | 수용. 레거시 START는 G3 판정 경로가 아니다 |
| n-6 | Minor | 보류 | notes §9. 부팅 로그 `tfw_main.c:257-283`과 sdkconfig grep을 첨부하는 것으로 대체 | 수용(§3-2 #5) |
| n-7 | Minor | 보류 | notes §9 | 사용자 장비 결정 대기. 연속 캡처가 안 되면 T-04/05/06은 NOT_RUN(거짓 합격 아님) |
| n-8 | Minor | 해결 | `tfw_pulse2.c:67,131,448` | `isr_lat_ns_max`는 여전히 태스크가 지운다(`:449`, notes에 명시). 수용 |
| 2차 이슈 1 (0x705E) | — | 해결 | `03_design/modules/TFW-03-tfw_modbus.md:11,33,55`(v0.2) | 보고서의 "설계 미기재" 표기는 더 이상 필요 없다 |

---

## 2. 신규 지적 (3차) — 모두 Minor

| ID | 심각도 | 파일:줄 | 근거 | 권고 |
|---|---|---|---|---|
| r3-1 | Minor | `host/tfw_host/scenario.py:256-268,281-282,297-299`, `tests/test-fw/test_round2_regress.py:90-95` | 신선도 증거에는 두 가지 한계가 있다. (1) 수정 시각은 하한만 본다. 앞선 캡처를 다시 내보내면(LA 소프트웨어가 마지막 캡처를 그대로 보유한 경우) 수정 시각이 새로 찍혀 검사를 통과한다. (2) 해시 비교는 같은 결과 폴더의 `T-*.json`만 본다. 그래서 결과 폴더를 바꾼 세션에서, 같은 seed(예시 시나리오의 seed는 고정값 12345/777/99)로 실행하고 캡처를 걸지 않은 채 다시 내보내면 T-04가 PASS가 될 수 있다. 또 `attach_capture`는 재판정할 때 앞선 `la_sha256`과 판정을 지운다. 시험(`:90-95`)도 FAIL 기록이 다른 CSV로 PASS가 되는 흐름을 정상으로 굳힌다. 같은 폴더 안에서는 다른 실행에 그 CSV를 붙이면 그쪽이 중복 FAIL이 되고, 보고서는 최악값을 쓰므로(`report.py:46-48`) 대부분 드러난다. 따라서 Minor로 둔다. | (a) T-04/05/06 실행마다 호스트가 0이 아닌 새 seed를 만들어 기록한다. 그러면 다른 실행의 캡처는 기대 엣지 수와 맞지 않아 FAIL이 된다. 이것이 가장 강한 방법이다. (b) 해시 원장을 결과 폴더 밖(예: `~/.tfw_capture_ledger`)에도 둔다. (c) 재판정 때 이전 `la_sha256`·판정을 `history`에 남기고, 이전 해시도 중복 검사 대상으로 유지한다. (d) 수정 시각 상한(다음 실행 시작 전)을 추가한다. |
| r3-2 | Minor | `components/tfw_logic/tfw_seq.c:54-56`, `tfw_pulse2.c:397,407`, `tests/test-fw/test_logic.c:605-616` | 대기 루프는 "busy인 축 없음 → break"를 `aborted()` 검사보다 **먼저** 한다. 실제 장치에서는 경로 A ISR이나 STOP_ALL이 `tfw_pulse2_stop`으로 `active = false`를 만들므로 busy가 곧바로 false가 된다. 그러면 마지막(또는 단일) 반복에서 트립이 나도 abort가 아니라 정상 종료로 처리되고, `seq_done++` 후 status가 **DONE**이 된다. fake hw는 중단 후에도 busy를 유지하므로(`t_r1_...`) 이 경로를 재현하지 못한다. T-04/05/06은 PCNT 부족으로 err > 0 → FAIL이 되므로 거짓 합격은 아니다. 다만 T-08/T-09 판정은 err를 보지 않는다. T-07 기록의 종료 상태도 경로에 따라 DONE과 IDLE로 달라진다. | 대기 루프를 빠져나온 뒤 `aborted()`를 한 번 더 확인해 abort로 처리한다. fake hw에 "ISR 정지 = busy false" 모드를 추가해 단일 이동 트립 → IDLE을 확인하는 시험을 넣는다. |
| r3-3 | Minor | `tfw_pulse2.c:326` 대 `:332,341`, `tfw_estop_io.c:53,73`, `tfw_logic` `tfw_es_pulses_after` | 시작 전 차단 검사(`:326`)와 PCNT clear(`:341`) 사이에 ISR이 들어오면 스냅샷(`count_isr`)은 이전 이동의 계수다. 그 뒤 clear되고 재검사(`:358`)까지 펄스가 몇 개 나간다. 그러면 `es_pulses_after`가 "최종 < 스냅샷 → 0"으로 **트립 후 펄스를 0으로 가린다**(2차 N-03 c와 같은 유형이지만 창이 수십 µs로 줄었다). T-07은 RECORD 항목이고 LA의 `la_pulses_after`가 기준 측정이므로 판정에는 영향이 없다. DC-08 기록값으로는 틀릴 수 있다. | ISR에서 "시작 중(starting)" 플래그를 보고 스냅샷을 무효로 표시한다(`es_pulses_after = 0xFFFFFFFF` = 미측정). 또는 `start_common`의 검사부터 clear까지 구간을 `portENTER_CRITICAL`로 감싼다. 보고서에서는 내부 값과 LA 값이 다르면 LA 값을 채택한다고 명시한다. |
| r3-4 | Minor | `host/tfw_host/scenario.py:163-168`, `tfw_estop_io.c:72-73`, `tfw_main.c:95-103` | `es_pulses_after`는 트립 10 ms 뒤 `es_task`가 계산한다. CLEAR_STATS로 지워지지도 않는다. 호스트는 status가 RUNNING을 벗어나면 바로 `read_results`를 읽으므로, 이론상 앞선 트립의 값을 이번 트립 값으로 기록할 수 있다. 읽기 순서상 약 20번의 Modbus 왕복 뒤라 확률은 낮다. | `s_pending`을 `es_state` 비트 등으로 내보내고, 호스트가 pending이 풀린 뒤 다시 읽게 한다. 또는 `latch_probe` 뒤(0.3 s 후) `ES_PULSES_*`를 다시 읽어 그 값을 기록한다. |
| r3-5 | Minor | `host/tfw_host/load.py:99-104`, `judge.py:179-180` | 연결이 유지된 채 타임아웃만 반복하는 클라이언트는 `MB_CLIENTS`에 계속 잡히고 요청도 한 건 이상이라 `idle`이 아니다. 따라서 PASS를 막지 않는다. 부하가 사실상 걸리지 않은 구간이 있어도 T-05가 PASS가 될 수 있다(오류는 사유로만 기록됨). 이 경우에도 펄스 판정 자체는 외부 계수로 하므로 "펄스 누락 PASS"는 아니다. 다만 "부하 중"이라는 조건이 약해진다. | 클라이언트별 성공 요청 수/초의 최솟값이나 타임아웃 비율 상한 [가정]을 PASS 전제로 두고 값을 기록한다. |
| r3-6 | Minor | `host/tfw_host/cli.py:31`, `scenario.py:336-359`, notes §4 6a | `--wait-capture` 안내 문구는 "내보낸 뒤 Enter"뿐이다. Enter 직후 다음 단계가 시작되므로, **다음 캡처를 먼저 걸어야 한다**는 안내가 없다. 옵션 없이 `run`을 실행하면 LA 단계 여러 개가 쉬지 않고 연속 실행되어, 6a의 "단계마다 별도 캡처" 절차를 따를 수 없다. 결과는 NOT_RUN 또는 FAIL 쪽으로 나오므로 거짓 합격은 아니다. | 안내 문구를 "캡처 내보내기 → 다음 단계 캡처 시작 → Enter"로 바꾼다. LA 단계가 2개 이상인데 `--wait-capture`가 없으면 경고하거나 중단한다. |
| r3-7 | Minor | `tfw_estop_io.c:100,127`, `judge.py:224-225` | `es_last_err`는 한 필드에 초기화 실패, ISR 서비스 기설치, 부분 바인딩 실패(그룹 1개 실패), 브레이크 동작 설정 실패, 해제 실패를 함께 담는다. 그래서 경로 B 바인딩 일부만 실패해도(설계 차이 #8의 bind_ok = 1 또는 2) 경로 A 측정까지 T-07 NOT_RUN이 된다. 보수적인 방향이라 거짓 합격은 아니지만, TFW-04 완료 기준 #1·#3(경로별 기록, 바인딩 결과 기록)의 실측을 막을 수 있다. | 초기화·ISR 설정 오류(경로 A 전제)와 바인딩·브레이크 오류(경로 B 전제)를 구분해 내보내고, 판정은 경로별 전제로 나눈다. |
| r3-8 | Minor | `host/tfw_host/scenario.py:227,232` | 한계값은 정확히 "1샘플 짧은 주기"에 해당하는 주파수다. 그런데 정상 출력도 양자화 때문에 1샘플 짧게 측정될 수 있다(예: 1 MHz, 50 MS/s → 0.98 µs). 이때 측정값과 한계값이 같고, 부동소수 오차로 `f_max > limit`가 참이 되면 FAIL이 날 수 있다. 보수적인 방향(거짓 불합격)이다. | 비교 시 반 샘플 여유를 두거나, 주기를 샘플 수(정수)로 바꿔 비교한다. |

---

## 3. 조건부 통과의 조건

### 3-1. HIL에서만 확인할 수 있는 항목 (사용자 실행)
1. **ESP-IDF 빌드**: iram_on, iram_off, TASK 모드, active-HIGH 빌드 각각. 스텁 문법 검사는 실제 헤더와 대조하지 않았다(notes §6·§9 남은 위험 1). 빌드 오류는 PASS가 아니라 실행 불가로 드러난다.
2. **IRAM 배치**: sdkconfig grep(notes §4 절차 3)과 `check_iram.sh`. 특히 `mcpwm_timer_start_stop`, `pcnt_unit_get_count`, `mcpwm_generator_set_force_level`이 iram_on 빌드에서 IRAM에 있어야 한다. 그렇지 않으면 경로 A를 force-level만 쓰도록 바꾼다(n-3).
3. **M-02 ISR 안 FPU**: notes §4 6b의 (a) 또는 (b).
4. **M-05 정지 후 첫 주기**: notes §4 6a. 단계별로 따로 캡처하고, 1 MHz → 5 kHz → 1 MHz 순서로 진행한다.
5. **N-03(a) 코어 간 경쟁**: 배치 A·B 각각, 운전 중과 이동 시작 직후에 트립을 넣는다. LA `la_pulses_after`로 트립 후 펄스 수를 확인하고, 내부 `es_pulses_after`와 비교한다(r3-3, r3-4).
6. **N-02 / M-03**: 부팅 로그에서 `es_last_err == 0`, 레벨 3/2/1 적용, IRAM 플래그 적용을 확인한다. P4에서 레벨 번호가 실제 우선순위로 작동하는지는 미확인이다.
7. **T-05 접속 6개**: `TFW_MAX_CLIENTS=8` 빌드에서 실제 HMI를 연결한다. 5번째 거부(TFW-03 #4)는 기본 4 빌드로 확인한다.
8. **외부 계수 장비 능력(n-7)**: 1,000회 운전 전체를 연속 캡처할 수 있는지 확인한다. 안 되면 T-04/05/06은 NOT_RUN으로 남는다(PASS 아님).
9. **[가정] 값 조정**: 캡처 길이 여유 0.5 s, 안정화 1 s, 첫 주기 허용 ±2샘플. 이 값들은 정상 캡처가 FAIL로 나오는 방향으로 틀릴 수 있다(notes §9 남은 위험 3).
10. **m-08**: 경로 A 단독 측정 시 OST 브레이크가 영향을 주는지 확인한다.

### 3-2. G3 근거 실측 동안의 운용 조건 (r3-1 코드 수정 전까지 필수)
1. 모든 G3 실측을 **결과 폴더 하나**(`--results results`)에 쌓는다. 세션 사이에 폴더를 지우거나 옮기지 않는다.
2. LA가 필요한 단계는 `run --wait-capture`로 실행한다. 단계마다 캡처를 새로 시작하고, 운전이 끝난 뒤 내보낸다(r3-6).
3. 같은 seed로 다시 실행해야 하면, 시나리오의 seed를 바꾸거나 앞선 결과와 같은 폴더를 쓴다.
4. FAIL 기록을 다른 CSV로 다시 판정하지 않는다. 불가피하면 원래 JSON을 복사해 보관하고 보고서에 사유를 적는다.
5. 보고서에 부팅 로그의 IRAM 옵션 줄(`tfw_main.c:257-283`)과 sdkconfig grep 결과를 첨부한다(n-6 대체).

---

## 4. 이슈 제기 (다른 단계·사용자 판단 필요)
1. (architect) 2차 이슈 2(비상정지 초기화 실패 동작)는 구현이 "영구 차단"으로 먼저 정했다(notes 설계 차이 #18). 이 동작을 TFW-04 제약에 반영할지 확정해 달라.
2. (architect) 2차 이슈 3(빌드 극성·옵션별 IRAM 비트 주소)과 r3-7의 경로별 오류 필드 주소 배정.
3. (사용자) 외부 계수 장비 결정(n-7). T-05 접속 수 결정 후보 DC-19(TFW-03 v0.2 §4).
4. (coder, 선택) r3-1(a) 실행마다 seed 생성과 r3-2(대기 후 abort 재확인)는 작은 수정으로 운용 조건 일부를 없앨 수 있다. 판정을 막지는 않는다.
