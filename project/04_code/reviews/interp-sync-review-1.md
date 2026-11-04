# 코드 리뷰: X·Y 동시 START/STOP(D-088) + X/Y 보간(D-089) — 1차

- 검토자: code-reviewer(독립 리뷰, 코드 수정 없음)
- 검토일: 2026-11-04
- 대상: 커밋 3bb64ab(D-088) + 작업 트리 미커밋 변경(D-089)
  - `firmware/components/fw_core/fw_interp.c`, `include/fw_interp.h`(신규)
  - `firmware/components/fw_motion/fw_motion.c`(s_ip, ip_*, start_all, op_axis_cmd, op_sys_cmd, motion_task)
  - `firmware/components/fw_motion/fw_axis.c`(ready/start_check, park, fw_axis_stream_*, set_servo, finalize, begin_decel, fw_axis_poll)
  - `firmware/components/fw_core/fw_regmap.c`(IPI, ip_valid, W_IP, 읽기 경로), `fw_hal/port_esp32p4/hal_io.c`(sync 입력)
- 참고: `fw_hal.h`, `hal_pulse.c`, `fw_stop.c/h`, `decisions.md` D-070·D-072·D-073·D-088·D-089, `03_design/interface-spec.md` §5.13·0x0100
- 제약: 실행 권한이 없어 시험을 돌리지 않았다. git diff도 실행할 수 없어 현재 작업 트리 파일을 직접 읽었다. `notes/firmware.md`, `notes/test-fw.md`에는 D-088·D-089 시험 결과가 없다.

## 요약

**판정: 수정 필요(재작업)**

보간기의 기본 구조는 타당하다. 락 아래에서 단일 주기로 돌고, 시작 게이트를 `start_blocked()`에 모았으며, STOP·알람은 매 주기 확인하고, 세션이 끝나면 `ip_hold`를 해제한다. 끝점 정수 수렴과 32비트 반쪽 쓰기 거부도 갖췄다. 그러나 아래 문제가 남아 있다.

1. **(Critical)** 보간 중 park되어 `MV_IDLE`인 축은 `SET_POSITION`을 받는다. 그러면 보간기가 위치 오차 전체를 축 최고 속도로, 가속 없이 따라잡으려 한다.
2. **(Major)** 동시 START의 all-or-nothing이 지켜지지 않는다. 사전 점검이 STOP 래치 해제 가능 여부를 보지 않아 한 축만 래치가 풀리고 펄스를 낸 뒤 거부될 수 있다.
3. **(Major)** 통신두절 때 "경로 감속"이 원호에서는 즉시 정지로 바뀐다. 알람 상태에서는 축을 다시 시작할 수 없기 때문이다.
4. **(Major)** 원호 끝점 불연속(최대 r×0.001 pulse)이 끝에서 속도 계단을 만든다.
5. **(Major)** park 중 BUSY/READY가 깜빡인다. park·방향 전환 때 펄스 폭이 잘리고 DIR 유지 시간이 지켜지지 않는다. DIR_SETUP 지연이 락 안에서 2 ms 주기를 넘을 수 있다.
6. **(Major)** fw_interp/stream/start_all 단위시험이 없다. 기존 `test_regmap.c:172`는 기대값이 옛 값 그대로여서 실패할 것으로 보인다.

중점 항목별로 정리하면 다음과 같다.
- 안전(1): 하드웨어 STOP 경로(MCPWM fault + ISR 래치)는 보간과 무관하게 유지된다. 보간기는 래치를 스스로 풀지 않는다(ip_tick이 `fw_hal_stop_blocking`을 먼저 보고 중단한다). 저장·재부팅 중 시작은 `start_blocked()`/`any_running()`이 막는다. 물리 START, 동시 START, 레거시 코일, Modbus 시작 명령은 park 순간에도 `s_ip.active`로 모두 거부된다. **시작이 아닌 축 명령**(SET_POSITION, SERVO_OFF, ALARM_RESET)만 열려 있다.
- 서보 출력(5): `s_ip.active=false`는 `ip_finish()` 한 곳에서만 일어나고, 거기서 두 축 모두 `fw_axis_stream_session(false)`를 부른다. `ip_start`도 실패할 수 있는 점검을 모두 끝낸 뒤에야 `ip_hold`를 켠다. **누락 경로는 찾지 못했다.**

## 지적 표

| ID | 심각도 | 파일:줄 | 내용 | 권고 |
|---|---|---|---|---|
| C-01 | Critical | fw_axis.c:389-394, fw_motion.c:187-197, fw_motion.c:149·159·166 | 보간 중 한 축이 park되면(저속 성분 `fabs(f)<IP_F_MIN && err<0.5`, 방향 전환 직전, 끝점 도달 후 다른 축 대기) `mv==MV_IDLE`이 된다. `FW_CMD_SET_POSITION`은 시작 명령이 아니어서(`fw_cmd_is_start`, fw_regmap.h:26) `fw_axis_command_gated`를 통과하고 `a->position`을 바꾼다. 다음 ip_tick에서 `err = tgt - (cur - base)`가 바뀐 양만큼 커진다. 그러면 `f = vff + 200·err`가 `axis_max_hz`로 잘린 채 **가속 없이 최고 속도로** 그 거리만큼 주행한다(예: 위치 50,000에서 0으로 리셋하면 50,000 pulse 급주행). 직전 커밋 dc6b4fd가 웹 UI에 "위치 0 리셋" 버튼을 넣어 실제로 누를 수 있다. | `op_axis_cmd`에서 `s_ip.active`이면 STOP/QUICK_STOP/SERVO_ON 외 축 명령(SET_POSITION, SERVO_OFF, ALARM_RESET, POINT_TEACH)을 0x13으로 거부한다. 방어 수단으로 `fw_axis_command`의 SET_POSITION·SERVO_OFF 조건을 `a->mv != MV_IDLE \|\| a->ip_hold`로 바꾼다. 웹 UI도 보간 중 버튼을 비활성화한다. |
| M-01 | Major | fw_motion.c:211-227, fw_axis.c:226-244, fw_axis.c:111 | D-088 all-or-nothing 위반. `fw_axis_ready_check`는 "래치됨 + 입력 해제"를 통과시키지만 `stable_ms >= FW_ES_RELEASE_STABLE_MS`(fw_stop.c:20)와 `release_hw`의 성공 여부는 보지 않는다. 속도 tick 범위도 보지 않는다. 그래서 X가 먼저 `start_move`에서 **래치를 풀고** `pulse_start`로 첫 펄스(TEZ 즉시 HIGH, hal_pulse.c:137)를 낸 다음, Y가 래치 해제 거부(입력이 방금 풀림)나 tick 오류로 실패할 수 있다. 이때 X는 QUICK_STOP으로 멈추지만 이미 펄스를 냈고, **거부된 명령이 X의 STOP 래치를 해제한** 상태로 남는다. D-072의 "거부된 명령은 래치를 풀지 않는다"(fw_axis.c:109-110 주석)와 어긋난다. | 2단계로 바꾼다. (1) 부작용 없는 점검을 완성한다: 래치 해제 가능(`!input && stable_ms>=…`), `fw_ticks_for_freq`(start/speed), target≠0, 절대 위치의 delta 범위. (2) 모든 축이 통과하면 그때 래치 해제 → 시작. 해제 후 시작 실패를 대비해 롤백 시 다시 래치하는 경로(relatch)를 둔다. 같은 점검을 `ip_start`에도 쓴다. |
| M-02 | Major | fw_motion.c:380-384, fw_axis.c:338-344, fw_axis.c:98, fw_axis.c:282-285, fw_motion.c:166-167 | 통신두절 때 `ip_stop(false)`(경로 감속) 직후 `fw_axis_decel_all_reason`이 두 축에 sticky `FW_AL_COMMLOSS`를 건다. 감속 중 park된 축이 다시 출발하거나 방향을 바꾸면 `start_move`가 `sticky_alarm != NONE`으로 `FW_RES_ALARM`을 돌려준다. 그러면 `ip_finish(ABORTED, …, QUICK)`이 나머지 축을 **현재 속도에서 즉시 park**한다. 원호에서는 한 축이 0을 지나 방향을 바꾸는 동안 다른 축이 최고 속도 근처이므로, 통신두절 정지가 사실상 급정지가 된다. 직선에서도 끝 수렴 단계(ended)의 재출발이 막혀 ABORTED로 끝난다. | 보간 세션 중에는 COMMLOSS 알람을 `ip_tick`이 처리하게 한다. stream 재출발 경로(`fw_axis_stream_run`)에서는 COMMLOSS sticky를 허용하고, 감속이 끝난 뒤 알람을 확정한다. 또는 감속 중 park를 금지한다(저속에서 park 대신 IP_F_MIN 유지). 시험: 원호 사분점 부근에서 통신두절을 걸어 감속 정지가 되는지 확인. |
| M-03 | Major | fw_interp.c:26-31, fw_interp.c:81-83, fw_motion.c:107·151-156, fw_motion.c:120-124 | 원호 끝점은 `max(2, r·1e-3)` pulse까지 원 밖에 있어도 받아들인다(fw_interp.c:27-28). 경로는 반지름 r인 원 위의 각 a1 지점에서 끝나는데 `fin`은 프로그램한 끝점(base+dx)이다. 그래서 ended 단계에 들어서는 순간 오차 e가 최대 r·1e-3만큼 튀고, `f = 200·e`가 가속 제한 없이 계단으로 나간다(r=200,000이면 200 pulse → 40 kHz 계단, 더 크면 축 최고 속도). 반대로 `ip_stop`이 t≥t2에서 불리면(`fw_ip_stop`이 아무것도 하지 않음, fw_interp.c:91) `fin`이 원 위의 점으로 바뀌어 프로그램한 끝점에 도달하지 못한다. 상태도 DONE이 아닌 STOPPED가 된다. | 끝점이 원 위에 정확히 오도록 경로를 만든다(반지름을 r0→r1로 선형 보간하는 나선, 또는 끝 각도에서 남은 오차를 경로 길이 전체에 분배). 허용 오차를 고정 소수 pulse(예: 2)로 줄이는 방법도 있다. `fw_ip_stop`이 t≥t2에서 무시될 때는 fin을 바꾸지 말고 상태도 RUN으로 둔다. ended 단계 보정 주파수에 가속 제한을 둔다. |
| M-04 | Major | fw_axis.c:451-467, fw_axis.c:253-263, fw_axis.c:128-131 | park 동안 `running=false`이므로 축 텔레메트리가 BUSY=0, READY=1, state=IDLE, `moving=0`으로 바뀐다. 보간 중 저속 성분 축이나 끝점에 먼저 도달한 축이 그렇다. PLC/HMI가 BUSY 하강을 축 완료로 해석하면 시퀀스가 잘못 진행된다. 재출발마다 `start_move`가 `pulses_done=0`, `leg_result=0`, `target_position_act = position ± INT32_MAX`를 다시 써서 이 값들도 의미가 없다. | 텔레메트리의 running/BUSY를 `a->mv != MV_IDLE \|\| a->ip_hold`로 정의하고 READY를 끈다. state는 POSITIONING(또는 보간 전용 값)으로 둔다. stream 재출발은 텔레메트리 초기화를 건너뛰고 `target_position_act`를 `s_ip.fin`으로 쓴다. interface-spec §5.13에 보간 중 축 상태 비트의 의미를 적는다. |
| M-05 | Major | fw_axis.c:253-263, fw_axis.c:282-284, fw_axis.c:119-120, hal_pulse.c:136-139·243-251, fw_params.c:12 | (a) `park()`의 `fw_hal_pulse_stop`은 출력을 강제로 LOW로 내린다. HIGH 구간 도중이면 짧은 펄스가 생기고, PCNT는 상승 엣지로 이미 세었다(hal_pulse.c:148). 드라이버가 최소 폭을 만족하지 못한 펄스를 무시하면 PCNT로는 감지할 수 없는 위치 오차가 생긴다. (b) 방향 전환 시 강제 LOW 직후 수 µs 안에 `fw_hal_dir_set`으로 DIR가 바뀐다. 펄스→DIR 유지 시간이 없어 하강 엣지로 세는 드라이버에서는 마지막 펄스가 반대 방향으로 셀 수 있다. 일반 이동에서는 정지와 재시작 사이에 최소 1주기가 있어 드러나지 않던 문제인데, 보간에서는 방향 전환마다(원호 1회당 최대 4회) 반복되어 오차가 쌓인다. (c) 재출발마다(같은 방향 포함) `esp_rom_delay_us(DIR_SETUP_US)`를 **motion 락을 쥔 채** 바쁜 대기로 기다린다. 범위가 1~1000 µs이므로 두 축이 같은 주기에 재출발하면 PCNT/MCPWM 재구성 비용을 더해 2 ms 주기를 넘을 수 있다. | park는 현재 펄스가 끝나기를 기다린다(다음 TEZ 직전 정지, 또는 compare 이후 정지 확인). 방향 전환 시 DIR 변경 전에 최소 유지 시간을 두고, 재출발을 다음 주기로 미루어 바쁜 대기를 없앤다. 같은 방향으로 재출발할 때는 DIR가 그대로이므로 지연을 생략한다. 보간 세션 중 park 대신 "주파수 최저 + 출력 게이트"로 멈추는 방식도 검토한다. T-시험에 원호 왕복 100회 후 실측 위치(엔코더 또는 드라이버 계수)와 PCNT의 차이를 추가한다. |
| M-06 | Major | tests/firmware/test_regmap.c:172, tests/firmware/Makefile:5, notes/firmware.md, notes/test-fw.md | `test_regmap.c:172`은 `wr1(0x100, 11)`이 3(예외)이기를 기대한다. 그런데 `FW_SYS_LAST`가 이제 `FW_SYS_INTERP_START=11`(fw_regmap.h:37-38)이어서 이 쓰기는 받아들여진다. 시험이 갱신되지 않았으므로 실패할 것으로 보인다(실행하지 않았음). Makefile은 `fw_interp.c`를 빌드에 넣었지만 `fw_ip_*`를 부르는 호스트 시험이 없다. `fw_axis_stream_*`, `start_all` 롤백, 0x0150 블록 쓰기 규칙(반쪽 쓰기·0x015E/F 쓰기·ip_valid 범위)을 다루는 시험도 없다. notes에도 D-088·D-089 결과가 없다. PC 쪽 test_web.py는 Python 시뮬레이터만 시험하므로 펌웨어 코드는 검증되지 않는다. | 다음을 추가한다. `test_regmap.c`: 12 → 3, 11·9·10 → 0, 0x0152 반쪽 쓰기 → 3, 0x015E 쓰기 → 2, feed 0/1,000,001, accel 9/60,001 → 3. `test_interp.c`: CCW/CW 사분원·반원·전체 원(end==start), 끝점 허용 오차 경계, 삼각 프로파일, `fw_ip_stop`(t<t1, t1~t2, ≥t2), `fw_ip_end_s`. 축 스텁 시험: park 중 SET_POSITION 거부(C-01), start_all 래치 롤백(M-01), 통신두절 원호(M-02). notes에 결과를 남긴다. |
| m-01 | Minor | fw_motion.c:89-93, fw_motion.c:132-137, fw_axis.c:229 | STOP이 래치되어 있고 입력만 풀린 상태에서 INTERP_START를 보내면 `ready_check`를 통과해 `FW_SRES_OK`, `ip_state=RUN`이 된다. 다음 주기 `ip_tick`이 `stop_blocking`을 보고 ABORTED(0x20)로 끝낸다. D-072("명시적 시작 명령은 래치 해제")와 다르게 동작하고, 성공 응답 뒤 곧바로 중단되어 사용자가 원인을 알기 어렵다. 안전 문제는 없다(래치를 풀지 않음). | D-072를 따른다면 ip_start에서 두 축 래치 해제를 2단계로 처리한다(M-01과 같은 방식). 따르지 않는다면 래치 상태에서는 0x10으로 거부하고 interface-spec에 예외로 적는다. |
| m-02 | Minor | fw_motion.c:42, fw_motion.c:139·146·155 | "IP_K·PERIOD<1000이면 과주행 없음"은 실제 주기 간격이 1/K=5 ms 이하이고 주파수 갱신이 다음 TEZ에 반영된다는 전제에서만 성립한다. 컴파일 시점 검사가 없어 `CONFIG_FW_UPDATE_MS>=5`이면 조용히 깨진다. Modbus 처리나 다른 태스크 때문에 락 대기가 길어지면 한 주기에 2 pulse 이상 나가 끝점을 넘을 수 있다(그 뒤 방향 전환으로 복귀하므로 M-05와 겹친다). vff는 명목 dt를 쓰고 t는 1 ms 단위로 양자화된다. | `_Static_assert(IP_K * CONFIG_FW_UPDATE_MS < 1000, …)`를 넣는다. ended 단계에서는 실제 경과 시간으로 남은 펄스 수를 넘지 않도록 주파수 상한을 `e / max_gap`으로 둔다. 실측 지터(T 시험)를 notes에 남긴다. |
| m-03 | Minor | fw_motion.c:94-99, fw_motion.c:159, fw_motion.c:172, fw_interp.c:24 | (a) 범위 검사가 끝점만 본다. 원호의 외접 사각형(중심 ± r)이 int32를 넘는지는 보지 않는다(ci·cj가 int32 전체 범위이면 r 최대 약 3.0e9). (b) `cur - s_ip.base[a]`는 int32 부호 있는 뺄셈이라 경로가 2^31을 넘으면 정의되지 않은 동작(UB)이다. (c) `(uint32_t)(s_ip.p.T * 1000.0)`은 T>약 49.7일(feed=1 pps처럼 낮을 때 가능)이면 double→uint32 범위 초과 UB다. | ip_start에서 원호 외접 사각형과 base의 합이 int32 안에 드는지 검사한다. 오차 계산은 `(int64_t)cur - base`로 한다. T 상한(예: 24 h)을 plan에서 FW_IP_E_RANGE로 거부한다. |
| m-04 | Minor | fw_motion.c:43, fw_motion.c:161-164, hal_pulse.c:123-126 | 저이송(feed<50 pps)이나 한 축 성분이 작은 직선은 IP_F_MIN=50 Hz 하한 때문에 "50 Hz 주행 ↔ park"를 반복한다. TEZ 갱신 방식에서는 50 Hz 주기(20 ms)가 끝날 때까지 새 주파수가 반영되지 않아 추종 오차가 커진다. 재출발할 때마다 M-05(b)(c)의 비용과 위험이 따른다. | 최소 feed나 축 성분 하한을 정해 문서화한다. 또는 저속 구간에서 park 대신 "다음 펄스 시각 예약" 방식(1 pulse 단발)을 쓴다. 스펙 §5.13에 저속 동작 특성을 적는다. |
| m-05 | Minor | fw_regmap.c:452-477, fw_axis.c:92-120 | 축 파라미터 쓰기(DIR_INVERT, DIR_SETUP_US, START_SPEED, SVON_MODE 등)는 운전 중에도 받아들여진다. 일반 이동에서는 다음 시작부터 적용되지만, 보간에서는 경로 중간의 재출발 때 바로 적용된다. 예를 들어 DIR_INVERT를 바꾸면 경로 중간부터 물리 방향이 뒤집히는데, PCNT 회계는 같아서 오차로 잡히지 않는다. | `s_ip.active` 동안 해당 축 par 쓰기를 거부(예외 06 또는 0x13)하거나, ip_start 시점의 파라미터 스냅샷을 stream 재출발에 쓴다. |
| m-06 | Minor | fw_motion.c:132-137, fw_axis.c:292-303, fw_axis.c:407-410 | STOP 입력이 한 축에 걸리면 `ip_finish`가 다른 축까지 park로 **즉시 정지**한다(설계 의도로 보이나 D-070 "다른 축은 건드리지 않음"과 다르므로 문서화가 필요하다). 다른 축은 최대 1주기(2 ms) 동안 혼자 계속 움직여 경로를 벗어난다. 중단을 park로 처리하므로 `finalize(QUICK, 2, 1)`의 `leg_fault=1`이 기록되지 않고, 축 `last_stop_cause`도 STOP 입력이 아닌 일반 QUICK이 된다. | interface-spec §5.13과 D-089에 "보간 중 한 축 STOP → 두 축 즉시 정지, 최대 1주기 지연"을 적는다. STOP 원인 축에는 finalize와 같은 텔레메트리(leg_fault=1)를 남긴다. |
| m-07 | Minor | hal_io.c:178-184, fw_motion.c:368-374 | 동시 STOP 입력(DI4)은 엣지로만 동작하고 START 극성(LOW 활성, 단선 시 비활성)을 따르므로 안전 정지가 아니다. D-088 (2)에 이미 별개라고 적혀 있다. 그러나 누르고 있는 동안에도 동시 START 엣지나 Modbus 시작이 막히지 않는다. 외부 입력 처리 결과가 `sys_cmd_result`를 덮어써서 Modbus 클라이언트가 읽는 자기 명령 결과와 섞인다. | 동시 STOP이 활성(레벨)인 동안 동시 START를 거부하도록 할지 결정해 문서화한다. 외부 입력 결과는 별도 레지스터나 이력에 남긴다. |
| m-08 | Minor | fw_motion.c:139-146, fw_interp.c:81-83 | ip_tick은 주기마다 `fw_ip_s` 2회와 `fw_ip_xy` 2회(원호면 sin/cos 4회)를 double로 계산한다. ESP32-P4 HP 코어의 FPU는 단정도라 double은 소프트웨어로 처리되는 것으로 알고 있다(미확인). 락을 쥔 채 실행되므로 실행 시간을 측정해야 한다. 2 ms 주기에 비하면 작을 것으로 예상하나 근거 수치가 없다. | 실물에서 `ip_tick`과 재출발 경로의 최악 실행 시간을 측정해 notes에 남긴다. 필요하면 각도 증분 회전(복소수 곱)으로 sin/cos 호출을 줄인다. |
| m-09 | Minor | fw_regmap.c:270-272·332-337, fw_motion.c:85, fw_motion.c:215-226 | 1축 모델에서도 0x0150 블록을 읽고 쓸 수 있다(INTERP_START만 0x17로 거부). START_ALL_REL/ABS도 1축에서 단축 시작으로 동작한다. interface-spec §5.13은 "2축 모델"이라고만 적었다. 동작 자체의 위험은 없다. | 1축 모델에서의 동작(블록 쓰기 허용 여부, 9/10의 의미)을 스펙에 적거나, 1축이면 블록 쓰기를 예외 02로 거부한다. |

## 확인된 양호 항목(참고)

- 시작 게이트: `start_blocked()`에 `s_ip.active`를 넣어 Modbus 축 시작(`fw_axis_command_gated`), 레거시 코일(`op_legacy_start`), 물리 START(motion_task 363행), 동시 START(371행), START_ALL(213행)이 모두 park 순간에도 거부된다. TEST keepalive 우회는 stream 축에서 `cmd_test`의 `mv != MV_IDLE`(fw_axis.c:217)에 걸려 거부된다.
- 저장·재부팅: `any_running()`이 `s_ip.active`를 포함하므로 SAVE, LOAD_DEFAULT, REBOOT, FACTORY/NET_RESET, 자동 저장이 보간 중에 막힌다. `s_saving`은 락 아래에서 켜지므로 저장 중에는 ip_start도 막힌다.
- STOP: 하드웨어 차단(MCPWM fault + ISR `fw_hal_pulse_stop`)은 보간과 무관하다. 보간기는 `fw_hal_stop_blocking` 확인을 stream_run보다 먼저 하므로 래치를 스스로 풀지 않는다. ISR이 그 사이에 걸려도 `start_move` 96행과 `pulse_start` 205·226행이 다시 막는다.
- `begin_decel`이 stream 축을 무시하므로 STOP_ALL이나 통신두절이 축 단위 감속 프로파일로 stream 축을 망가뜨리지 않는다.
- 레지스터: 32비트 항목 반쪽 쓰기는 `a != t.istart`로 3(fw_regmap.c:414). 0x015E/F 쓰기는 W_UNDEF로 2이고 원자적으로 거부된다. ip_valid는 블록 전체 후보를 검사한다(feed 1~1,000,000, accel/decel 10~60,000, mode≤3, abs≤1). 0x0110 시스템 블록(5워드)과 겹치지 않는다.
- 원호 각도: `turn()`은 (0, 2π]로 정규화하고 end==start이면 한 바퀴다. CW는 `-turn(a1, a0)`로 맞다.
- 서보: `ip_hold` 해제 누락 경로 없음(요약 참조).

## 이슈 제기

1. **G2 변경 승인 전 구현(D-089):** D-089는 SRS v0.9의 FR-014·CON-010·Q8을 바꾸는 범위 변경이며 G2 변경 승인이 "대기"로 기록되어 있다. 이번 지적(C-01, M-01~M-05)을 고치기 전에는 실기 시운전을 하지 않을 것을 권고한다. 게이트 기록은 사용자가 작성한다.
2. **설계 문서 부재:** `project/03_design/modules/`에 보간기와 동시 시작 모듈 설계서(완료 기준·제약)가 없다. 이번 리뷰는 interface-spec §5.13과 0x0100 행, D-088·D-089를 기준으로 했다. 완료 기준(끝점 오차, 경로 추종 오차 한계, 저속 동작, 통신두절 동작)을 설계서로 정해 달라.
3. **interface-spec 갱신 필요(③ 문서, 메인 세션):** 보간 중 축 상태 비트의 의미(M-04), 보간 중 한 축 STOP 시 두 축 즉시 정지(m-06), 1축 모델 동작(m-09), 원호 끝점 허용 오차가 끝 동작에 미치는 영향(M-03).
4. **웹 UI(pc-tool):** "위치 0 리셋" 버튼(커밋 dc6b4fd)을 보간·동시 운전 중 비활성화해야 한다(C-01의 유발 경로). 이번 리뷰 범위 밖이라 코드는 보지 않았다.
5. **실물 확인 필요(미확인):** 드라이버의 펄스 계수 엣지·최소 펄스 폭·DIR 유지 시간(M-05), ESP32-P4의 double 연산 비용(m-08), motion 주기 지터(m-02).
