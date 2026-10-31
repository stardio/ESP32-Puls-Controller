# 펌웨어(2축 제품) 독립 리뷰 #1

| 항목 | 내용 |
|---|---|
| 검토자 | code-reviewer (2026-10-31) |
| 대상 | `project/04_code/firmware/`, `project/04_code/tests/firmware/`, `project/04_code/notes/firmware.md` |
| 기준 | design-baseline-v0.4.md §2·§3·§7, interface-spec.md §5, decisions.md D-056·D-058·D-059·D-069·D-070·D-072 |
| 시험 결과 | 실행하지 않았다(권한 없음). notes §10, 메인 세션 확인: make run 0 failures(8,689/424/204 checks), pytest 31 passed |
| 한계 | IDF 빌드·실장비 미검증(notes §8 U1~U13). IDF API 동작에 관한 지적은 IDF 5.x 공개 동작 기준이며 T-07·T-15에서 확인해야 한다 |

## 판정: **조건부 통과**

Critical은 없다. 구조(GPIO33 첫 문장 LOW, 필수 초기화 AND 뒤 허용, 축별 ISR + fault 이중 경로, N-03 순서, 후보 이미지 원자 쓰기, TCP/RTU 배타 시작)는 설계를 따른다.
다만 결정 미반영 2건(D-072, D-059)과 안전·경쟁 Major 4건이 있다. **M-01~M-06을 고치고 시험을 보강한 뒤 IDF 첫 빌드·HIL로 가는 것**을 조건으로 한다. Minor는 HIL 전후 언제 처리해도 된다.

## 지적 요약

| ID | 심각도 | 위치 | 요지 |
|---|---|---|---|
| M-01 | Major | fw_axis.c:89, hal_io.c:108-130, fw_stop.c:18-23 | D-072 미반영: HMI/Modbus 시작 명령이 래치를 풀지 못함 |
| M-02 | Major | fw_axis.c:230, hal_io.c:117-129, fw_stop.c:13-17 | STOP 엣지 ISR을 놓치면 운전 중 축이 멈추지 않음(레벨 감시 없음) |
| M-03 | Major | fw_motion.c:195-199, 114-121, 196-203 | 저장/재부팅 판단과 실행 사이에 락이 풀려 운전 중 NVS 쓰기·재부팅 가능 |
| M-04 | Major | fw_params.c:49, fw_motion.c:92 | D-059 위반: RTU 모델 출하 기본값이 TCP |
| M-05 | Major | fw_regmap.c:251-256·343-349, fw_motion.c:84-88 | 네트워크·통신 모드 쓰기가 잠금(net_apply) 없이 자동 저장되어 재부팅 시 적용 |
| M-06 | Major | hal_io.c:180-188, hal_io.c:111-112 | D-070 1차 차단(MCPWM fault) 실패가 로그뿐, 레지스터·운전 허용에 반영 안 됨. 복구 실패 후에도 기동 |
| m-01 | Minor | sdkconfig.defaults:36, hal_sys.c:38-50 | T-15: 패닉 출력 중 펄스·GPIO33 유지, WDT 진입 시 출력 차단 훅 없음 |
| m-02 | Minor | fw_motion.c:174, hal_io.c:132-139 | WDT 구독 반환값 무시, io_task 미감시 |
| m-03 | Minor | hal_pulse.c:146-148, 82-84 | 목표 도달 시 마지막 펄스 HIGH 폭이 ISR 지연으로 잘림 |
| m-04 | Minor | fw_motion.c:184, fw_motion.c:280 | 물리 START가 래치를 푼 뒤 시작이 버려지는 경로 2개 |
| m-05 | Minor | hal_io.c:194-196 | GPIO ISR 서비스 IRAM 플래그가 PCNT 옵션에 묶임 |
| m-06 | Minor | fw_motion.c:192 | `s_cl_tripped`가 해제되지 않음(sys_status b6 영구) |
| m-07 | Minor | fw_mb_rtu.c:24-30 | RTU 프레임 끝 판정이 태스크 틱 기반 |
| m-08 | Minor | fw_mb_tcp.c:103, 52 | 접속 수 상한 경쟁 |
| m-09 | Minor | fw_regmap.c:432 | FC01/02 개수 상한 64 |
| m-10 | Minor | fw_regmap.h:11, notes §6 | 신규 주소 7개 문서화·regmap_version |
| T-01 | Major(시험) | test_axis.c:225 외 | D-072 반대 동작을 단언, M-01~M-03 경로 시험 없음 |

---

## 상세

### M-01 [Major] D-072 미반영: HMI/Modbus 시작 명령이 STOP 래치를 해제하지 못함
- **근거:** D-072는 "STOP 입력이 해제된 상태에서 들어온 명시적 시작 명령(물리 START 엣지, 또는 HMI/Modbus 운전 시작 명령)은 모두 새 START로 보고 래치를 해제하며 운전을 시작한다"고 정했다. 현재 `start_move()`는 `fw_hal_stop_blocking()`(래치 OR 입력)이 참이면 바로 0x10으로 거부한다(fw_axis.c:89). 래치 해제는 io_task의 물리 START 엣지 경로에만 있다(hal_io.c:124-126 → fw_stop.c:42). 시험도 이전 동작을 단언한다(test_axis.c:225 "the HMI cannot restart either").
- **ESTOP_RELEASE:** fw_motion.c:78-79의 "항상 거부(0x10)"는 D-072("별도 해제 명령은 두지 않는다")와 **일치하므로 그대로 둔다**. 바꿀 곳은 시작 명령 경로다.
- **고칠 위치와 수정안:**
  1. `fw_hal.h` + `hal_io.c`(release_hw 근처, 108행 뒤): `int fw_hal_stop_release(uint8_t axis)`를 새로 만든다. 동작: 입력 비활성이고 `stable_ms ≥ FW_ES_RELEASE_STABLE_MS`일 때만 `fw_es_release()`를 하고, 이어서 `release_hw()`를 한다. `mcpwm_operator_recover_from_fault` 실패 시 래치를 다시 세우고 오류를 돌려준다(M-06과 연결). 반환: 0 해제됨 / FW_ES_REFUSED.
  2. **ISR 경쟁 방지:** `fw_es_release()`(fw_stop.c:20-21)는 "입력 확인 → latched=0"이 원자적이지 않다. 확인과 기록 사이에 stop_isr(hal_io.c:104)가 트립하면 그 트립이 지워진다. 해제 함수와 `stop_isr`를 같은 `portMUX_TYPE`(DRAM)로 감싼다(태스크 쪽 `portENTER_CRITICAL`, ISR 쪽 `portENTER_CRITICAL_ISR`). 기존 io_task 경로(hal_io.c:121-126)도 같은 함수를 쓰게 해서 `release_hw` 중복 호출을 없앤다.
  3. `fw_axis.c:88-100` `start_move()`: 89행의 STOP 검사를 **100행 뒤(모든 다른 거부 조건 뒤, 108행 dir_set 앞)**로 옮기고 다음으로 바꾼다.
     - `fw_hal_stop_input(idx)` 참 → 0x10 거부(래치 유지)
     - `fw_hal_stop_latched(idx)` 참 → `fw_hal_stop_release(idx) != 0`이면 0x10 거부, 0이면 진행
     - 순서를 뒤로 두는 이유: 알람·서보 OFF·범위 오류로 거부되는 명령이 래치만 풀고 끝나는 일이 없게 한다.
     이 한 곳으로 START_ABS/REL(fw_axis.c:190-191), 레거시 코일 0(fw_regmap.c:393 → fw_motion.c:63-68 → fw_axis.c:145-150)이 모두 해당된다. `fw_hal_pulse_start()`의 재확인(hal_pulse.c:205, 226)은 그대로 둔다.
  4. 그대로 둘 것: ALARM_RESET 거부(fw_axis.c:201), STOP 활성 중 시작 거부, ESTOP_RELEASE 거부.
  5. 주석·문서: fw_stop.h:7-8, hal_io.c:6, notes R7·§7 이슈 1을 D-072 기준으로 고친다. interface-spec §5.3 `estop_state`=2 "해제 명령 대기", §5.8 0x0101 "입력 해제 + ESTOP_RELEASE"는 D-072와 맞지 않는다 → 아래 「이슈 제기」.

### M-02 [Major] STOP 엣지를 놓치면 운전 중인 축이 계속 펄스를 낸다(레벨 감시 없음)
- **근거:** 래치는 엣지 ISR(hal_io.c:101-106, POSEDGE/NEGEDGE 199행)과 부팅 검사(221-222행)에서만 세워진다. 1 ms 폴링 `fw_es_input_sample()`(fw_stop.c:13-17)은 입력이 활성이어도 래치를 세우지 않는다. `fw_axis_poll()`은 운전 중 `latched`만 본다(fw_axis.c:230). poll_once의 보호(hal_io.c:127)도 `latched`에 달려 있다. 그래서 엣지가 빠지면(노이즈 짧은 펄스 뒤 레벨 유지, ISR 서비스가 이미 다른 플래그로 설치된 경우(204행), 핸들러 추가와 부팅 검사 사이 등) 입력이 활성인데도 소프트웨어 경로가 차단하지 않는다. 시작 거부는 레벨도 보므로(hal_io.c:86) 새 기동만 막히고 **진행 중 이동은 막히지 않는다**. fault 경로(B)가 묶이지 않았으면(M-06) 남는 차단이 없다. 기준선 §3 "해당 축 펄스 즉시 차단"에 어긋난다.
- **수정안:** poll_once(hal_io.c:122 뒤)에서 `stop_in && !s_es[ax].latched`이면 `fw_es_trip()` + `fw_hal_pulse_stop()`(M-01의 mux 안에서). `fw_axis_poll()` 230행 조건을 `latched || fw_hal_stop_input()`로 넓힌다. 시험: fake_hal에서 ISR 없이 입력만 활성화했을 때 1 ms 안에 정지·래치되는지.

### M-03 [Major] 지연 저장·재부팅의 판단과 실행 사이에 락이 풀려 운전 중 NVS 쓰기·재부팅이 가능하다
- **근거:** motion_task는 락을 잡은 채 `running=false`로 `save_now`·`do_reboot`를 정하고(fw_motion.c:189-196), 197행에서 락을 푼 뒤 199행 `do_save()`에서 다시 잡는다. 그 사이에 Modbus 태스크가 락을 얻어 START를 처리할 수 있다(`s_saving`이 아직 false라 op_axis_cmd 57행 통과). 그 뒤 `do_save()`는 운전 여부를 다시 보지 않고 `s_saving=true` 후 플래시를 쓴다(114-124행). 결과적으로 notes §3·hal_store.c:1-3이 보장한다는 "운전 중 저장 없음"이 깨진다. NVS 쓰기 동안 motion_task 자신이 막혀 있으므로 2 ms 프로파일 갱신·감속 시작(fw_axis.c:239-245)이 멈춘다. 목표 정지는 PCNT ISR(IRAM)이 하므로 펄스 수는 지켜지지만, 감속 없이 고속에서 끊길 수 있다(스텝 손실·드라이브 알람). 재부팅 경로(196, 200-203행)도 같은 창이 있어 이동 중 `esp_restart()`가 가능하다.
- **수정안:** (a) 195행에서 `save_now`가 참이면 **락 안에서** `s_saving=true`로 바꾸고 do_save는 이미 세워진 플래그를 쓴다. 또는 (b) do_save 117행 직후 `if (any_running()) { unlock; return; }`(요청은 남긴다). 재부팅은 `s_reboot_req` 동안 시작 명령을 거부하거나(op_axis_cmd·op_legacy_start·물리 START) 락 안에서 재확인한다. fw_motion.c는 호스트 시험 대상이 아니므로 순서 논리를 fw_core로 빼거나 HIL 항목(T-06)에 "저장 직전 START"를 넣는다.

### M-04 [Major] D-059 위반: RTU 모델의 출하 기본 통신 모드가 TCP
- **근거:** D-059 "출하 기본값 TCP 모델=TCP, RTU 모델=RTU". 기본값 표는 모델과 무관하게 `FW_MODE_TCP`다(fw_params.c:49). notes:138도 "모델과 무관하게 TCP"라고 적었다. `LOAD_DEFAULT`(fw_motion.c:92), 첫 부팅·설정 손상 시 기본값 적재(fw_motion.c:228 `fw_dev_init` → `fw_cfg_defaults`)도 같다.
- **수정안:** `fw_dev_init()`(fw_regmap.c:82-88) 또는 `fw_cfg_defaults` 호출 뒤에 `if (model_rtu) cfg.net[FW_N_COMM_MODE] = FW_MODE_RTU;`를 둔다. LOAD_DEFAULT 경로(fw_motion.c:92)에도 같은 처리를 넣는다. notes R13·§9를 고치고, test_regmap/test_core에 "RTU 모델 기본값=RTU" 단언을 추가한다.

### M-05 [Major] 네트워크·통신 모드 쓰기가 잠금 없이 자동 저장되어 재부팅 시 적용된다
- **근거:** interface-spec §5.10은 `net_apply`(0x400F)를 "1 = 저장 후 재기동 시 반영(잠금 필요)"로 정한다. 코드는 0x4000~0x400E 일반 쓰기를 W_SEG로 받아(fw_regmap.c:254-255) `cfg_changed`를 부르고(346-349행), 2 s 뒤 자동 저장한다(fw_motion.c:71, 195). 그래서 잠금과 net_apply 없이 FC06 한 번으로 IP·유닛 ID·`comm_mode`(0x400E)가 바뀌고, 다음 전원 투입 때 적용된다. RTU 모델에서 0x400E=1을 잘못 쓰면 TCP HMI가 다음 부팅부터 접속하지 못한다(복구는 USB 도구뿐).
- **수정안:** 네트워크 세그먼트 변경은 저장 대상에서 따로 표시해 `cfg_changed`(자동 저장)를 부르지 않는다. 저장은 잠금을 확인한 `NET_APPLY`(fw_motion.c:84-88)에서만 한다. 또는 W_SEG segid 3 쓰기 자체에 `fw_dev_unlocked()`를 요구한다(예외 03 또는 sys_cmd_result 0x11). 어느 쪽이든 시험에 "잠금 없이 0x400E 쓰기 → 저장 요청 없음"을 넣는다.

### M-06 [Major] D-070 1차 차단(MCPWM fault) 실패가 드러나지 않고, fault 복구 실패 뒤에도 기동한다
- **근거:** D-070은 1차 차단을 MCPWM fault 경로로 정했고, r2 보드에는 축별 AND 게이트가 없다(D-069). fault 생성·brake·generator 설정이 실패해도 로그만 남기고(hal_io.c:188) `boot_status`, 알람, GPIO33 허용에 반영하지 않는다. HMI에서 이 저하를 알 수 없다. `release_hw()`에서 `recover_from_fault` 실패도 경고 로그만 남긴다(hal_io.c:111-112). 이 경우 래치는 풀리고 START가 받아들여진다. OST brake가 남아 있으면 출력 LOW·PCNT 0으로 이동이 끝나지 않고, 축이 MV_RUN에 머문다(fw_axis.c:229-250에 정체 감시 없음).
- **수정안:** boot_status에 축별 "fault 경로 묶임" 비트를 추가한다(예: b6 X, b7 Y; notes §4·regmap 반영). [권고 기본값] 묶이지 않은 축은 운전 금지(`FW_AL_PE_HW`)로 한다. T-07에서 경로 B가 확인되기 전까지 소프트웨어 경로만으로 운전하는 것은 기준선 §3 의도와 다르다. 다르게 정하려면 Kconfig로 명시적으로 허용한다. recover 실패 시 래치를 유지하고 0x10으로 거부한다(M-01 해제 함수에서). 보조로 "운전 중 N ms 동안 계수 증가 없음 → 즉시 정지 + 알람" 감시를 fw_axis_poll에 추가한다.

### m-01 [Minor] T-15 대비: WDT 진입부터 리셋까지 출력이 계속 나간다
- **근거:** 패닉 = `CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT`(sdkconfig.defaults:36), 콘솔 115200. 백트레이스를 출력하는 동안(수십~수백 ms [가정], T-15 측정 대상) MCPWM은 마지막 주파수로 계속 돌고 GPIO33도 HIGH다. TWDT 1000 ms(hal_sys.c:40-44)를 더하면 최악 약 1.x s 동안 출력이 이어진다. 목표 정지는 PCNT ISR이 살아 있으면 지켜지지만, 감속은 없다.
- **수정안:** TWDT ISR 사용자 훅(`esp_task_wdt_isr_user_handler`, IDF weak 심볼, IRAM)에서 GPIO33 LOW와 두 generator force LOW를 하도록 구현한다. 출하 빌드는 `CONFIG_ESP_SYSTEM_PANIC_SILENT_REBOOT` 검토. T-15를 하려면 시험 빌드 전용(Kconfig) 정지 유도 명령(태스크 무한루프 / 인터럽트 금지 루프)이 필요하다. 지금은 없다.

### m-02 [Minor] WDT 구독 결과 무시, io_task 미감시
- **근거:** `fw_hal_wdt_add_self()`(hal_sys.c:51) 반환을 버린다(fw_motion.c:174). 실패하면 감시 없이 운전이 허용된다. io_task(STOP 레벨·START·래치 해제, hal_io.c:132-139)는 구독하지 않는다.
- **수정안:** 구독 실패 시 `fw_hal_oe_forbid()` + FW_BOOT_WDT 해제. io_task도 구독하거나, io_task 생존 카운터를 motion_task가 확인한다.

### m-03 [Minor] 목표 도달 시 마지막 펄스 HIGH 폭이 잘린다
- **근거:** PCNT는 상승 엣지를 센다(hal_pulse.c:148). N번째 상승 엣지에서 ISR이 force LOW를 걸어(82-84행) 마지막 펄스 HIGH 폭이 ISR 지연(수 µs [가정])으로 줄어든다. 드라이브 최소 펄스 폭보다 짧으면 1펄스가 누락될 수 있다. servo1axis(motion_core.c:114)와 같은 방식이고 1 MHz 실적이 있어 Minor로 둔다.
- **수정안:** T-03/T-04에서 마지막 펄스 폭을 LA로 확인한다. 문제가 있으면 하강 엣지 계수(POS=HOLD, NEG=INCREASE)로 바꾼다.

### m-04 [Minor] 물리 START가 래치를 푼 뒤 시작이 버려지는 경로
- **근거:** io_task가 래치를 먼저 풀고(hal_io.c:124-126) 콜백으로 요청만 남긴다. `s_saving` 중에는 요청을 버린다(fw_motion.c:184). 또 `fw_hal_set_start_cb()`가 io_task 생성(fw_motion.c:278) 뒤인 280행에 있어, 그 사이 엣지는 콜백 없이 래치만 푼다.
- **수정안:** start_cb를 `fw_hal_io_init` 전에 등록한다. 저장 중 물리 START는 보류(pending 유지)하거나 래치 해제를 motion_task 쪽(M-01 공통 함수)으로 옮긴다.

### m-05 [Minor] GPIO ISR 서비스 IRAM 플래그가 PCNT 옵션에 묶여 있다
- **근거:** hal_io.c:194-196 `#if CONFIG_PCNT_ISR_IRAM_SAFE`. STOP ISR의 IRAM 성질이 무관한 옵션에 의존한다.
- **수정안:** 무조건 `ESP_INTR_FLAG_IRAM`을 쓰거나 전용 Kconfig(`FW_STOP_ISR_IRAM`, 기본 y)를 두고, test_cfg_syntax에 단언을 추가한다.

### m-06 [Minor] 통신두절 표시가 해제되지 않는다
- **근거:** `s_cl_tripped = true`(fw_motion.c:192)를 지우는 코드가 없다. ALARM_RESET 뒤에도 sys_status b6이 남는다.
- **수정안:** 모든 축의 FW_AL_COMMLOSS 알람이 해제되면 지운다.

### m-07 [Minor] RTU 프레임 끝 판정이 틱 기반이다
- **근거:** `uart_read_bytes(..., pdMS_TO_TICKS(s_gap_ms))` 반복(fw_mb_rtu.c:27). 1 kHz 틱에서 2틱 대기는 실제 1~2 ms다. 19200 bps 이상에서 t3.5(1.75 ms 규정값) 경계가 흔들린다. t1.5도 검사하지 않는다. notes U8에 이미 적혀 있다.
- **수정안:** `uart_set_rx_timeout()`(문자 단위) + UART 이벤트 큐의 `UART_DATA`/TOUT 기반으로 바꾼다. HIL에서 연속 폴링 마스터로 확인한다.

### m-08 [Minor] TCP 접속 수 상한 경쟁
- **근거:** 카운트 증가는 client_task 안(fw_mb_tcp.c:52)에서 하고, 검사는 accept 루프(103행)에서 한다. 빠른 연속 접속은 상한을 넘을 수 있다.
- **수정안:** accept 루프에서 증가시키고, 태스크 생성 실패 시 감소시킨다.

### m-09 [Minor] FC01/02 요청 개수 상한 64
- **근거:** fw_regmap.c:432는 n>64를 예외 03으로 거부한다. 규격 상한은 2000이고 servo1axis는 COIL_COUNT 기준으로 예외 02다(movion_modbus_tcp.c:339-340). 실제 HMI가 큰 블록을 읽으면 호환 차이가 생긴다.
- **수정안:** 정의 범위 밖은 02, 개수 0 또는 2000 초과는 03으로 맞춘다. 레거시 창 호환 기준(예외 코드)을 notes에 적는다.

### m-10 [Minor] 스펙 밖 신규 레지스터 7개
- **판단:** 0x002C reset_reason, 0x002D boot_status, 0x002E 실제 통신 모드, 0x0031 di_raw, 0x0032 do_state는 시스템 상태(R) 영역(§5.2)의 빈 주소다. 0x0120 DO3/DO4는 명령 영역의 빈 주소다. 0x400E comm_mode는 §5.10 0x400D~0x400E 빈칸이다. 기존 주소와 겹치지 않고 읽기 전용·쓰기 검증(fw_regmap.c:325, fw_params.c:130)도 맞다. T-15(리셋 원인)·부팅 실패 진단(N-02)·D-059(모드)를 HMI에 보여 주는 데 필요하므로 **타당**하다.
- **조건:** (1) 0x400E는 M-05의 잠금 규칙을 따라야 한다. (2) M-06의 fault 비트를 boot_status에 추가하면 함께 문서화한다. (3) 추가는 기존 주소를 바꾸지 않으므로 `regmap_version`(fw_regmap.h:11)을 2로 두는 것은 §5.1 규칙상 허용된다. 다만 regmap.yaml과 interface-spec §5.3·§5.10에 올려야 한다(메인 세션). (4) di_raw의 예비 DI 극성은 START 극성을 그대로 쓴다(hal_io.c:218, 249-254). 보드 실측(U10) 전까지 "미확인"으로 표시한다.

### T-01 [Major(시험)] 시험 보강
- test_axis.c:225는 D-072와 반대 동작(HMI 재기동 불가)을 단언한다 → "STOP 해제 10 ms 뒤 HMI START_REL·레거시 코일 0 → 래치 해제 + 운전", "해제 10 ms 이내/입력 활성 중 → 0x10 + 래치 유지", "다른 사유(서보 OFF·범위)로 거부되면 래치 유지"로 바꾸고 추가한다.
- M-02: ISR 없이 레벨만 활성 → 정지·래치(fake_hal에 "ISR 누락" 모드 추가).
- M-04: RTU 모델 기본값·LOAD_DEFAULT 뒤 comm_mode=1.
- M-05: 잠금 없이 0x4000/0x400E 쓰기 → cfg_changed 미호출(또는 거부).
- M-06: fault 미바인딩·recover 실패 → 기동 거부.
- M-03은 fw_motion.c가 호스트 시험 밖이라 지금은 증명할 수 없다. 순서 논리를 fw_core로 빼서 시험하거나 T-06에 넣는다.

## 중점 항목별 결론

| 중점 | 결론 |
|---|---|
| (1) 안전 | 축별 ISR → 해당 축만 force LOW, GPIO33 미접촉(hal_io.c:101-106) 적합. STOP 중 START 거부(fw_axis.c:89, hal_pulse.c:205·226) 적합. 부팅 실패 시 GPIO33 LOW 유지(app_main.c:16, fw_motion.c:42-50·283·289-292) 적합. 미흡: 레벨 백업 없음(M-02), fault 실패 은닉(M-06), WDT 진입 시 차단 훅 없음(m-01) |
| (2) D-072 | 미반영. 고칠 곳: fw_axis.c:89(→100행 뒤로 이동), hal_io.c:108 근처 신규 `fw_hal_stop_release` + stop_isr mux, fw_hal.h 선언, fake_hal.c, test_axis.c:225. ESTOP_RELEASE 거부(fw_motion.c:78-79)는 D-072와 일치하므로 유지(M-01) |
| (3) ISR·경쟁·2축 | ISR은 IRAM, DRAM, RTOS 미사용이고 인라인 계산(fw_profile.h:9-36)도 적합. 2축 자원은 MCPWM 그룹=축, PCNT 유닛 축별, 같은 intr_priority라 충돌이 없다. 경쟁: 래치 해제 대 ISR(M-01-2), 저장 창(M-03) |
| (4) Modbus | 레거시 창(쓰기 가능 주소·02/03·반쪽 쓰기·코일 0~3·DI 0~7·유닛 ID 0x0B)이 servo1axis와 일치한다. TCP/RTU 배타 시작(fw_modbus.c:10-26), U32 하위 워드 먼저(§5.1)도 적합. 미흡: M-04, M-05, m-07~m-09 |
| (5) NVS | 정상 경로는 정지 중에만 쓰고 저장 중 START를 거부한다. IRAM 옵션도 있다(sdkconfig.defaults:21-26, U2로 확인 필요). 판단~실행 창(M-03)은 고쳐야 한다 |
| (6) 신규 레지스터 | 타당(m-10). 0x400E 잠금, 문서 반영 조건 |

## 이슈 제기
1. interface-spec §5.3 `estop_state`=2 "해제 명령 대기", §5.3 sys_command 1 ESTOP_RELEASE, §5.8 0x0101 해제 "입력 해제 + ESTOP_RELEASE"는 D-058·D-072와 맞지 않는다. ③ 문서 갱신 필요(메인 세션).
2. interface-spec §5.10에 0x400E comm_mode, rtu_baud ÷100 인코딩(notes R12)을 반영해야 한다. 네트워크 쓰기 잠금 규칙(M-05)을 명확히 해야 한다.
3. D-070: fault 경로 미바인딩 시 운전 허용 여부를 권고 기본값으로 기록해야 한다(M-06 권고: 금지).
