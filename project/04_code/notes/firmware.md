# 2축 제품 펌웨어 구현 노트 (firmware)

| 항목 | 내용 |
|---|---|
| 작성 | coder (2026-10-31) |
| 근거 | design-baseline-v0.4.md, interface-spec.md §5(regmap v0.2), D-069, D-071(게이트 예외, 사용자 지시) |
| 위치 | `project/04_code/firmware/`(소스), `project/04_code/tests/firmware/`(호스트 시험) |
| 상태 | **IDF 빌드·실장비 미검증.** 호스트 시험(순수 로직 + 가짜 HAL)과 IDF 스텁 문법 검사만 통과 |

## 1. 구조

servo1axis의 `app_main → motion → Modbus` 구조를 따르고, 하드웨어 의존부는 `fw_hal` 뒤에 둔다(DC-14).

| 컴포넌트 | 역할 | IDF 의존 | 호스트 시험 |
|---|---|---|---|
| `fw_core` | 순수 로직: 프로파일·주기 계산, STOP/START 상태 기계, 파라미터 표·범위·CRC·모드 선택·지연 저장·통신두절, 레지스터 맵 PDU 처리 | 없음 | 직접 |
| `fw_hal` | `include/fw_hal.h`(추상 계층), `include/fw_cfg.h`(Kconfig → 0/1 매크로), `port_esp32p4/`(MCPWM+PCNT, GPIO ISR/fault, DO/DI, NVS, WDT, 리셋 원인) | port만 | 가짜 HAL(`tests/firmware/fake_hal.c`) |
| `fw_motion` | `fw_axis.c` 축 상태 기계(HAL만 사용), `fw_motion.c` 부팅 순서·장치 모델·운전 태스크·지연 NVS 저장·통신두절 정지 | `fw_motion.c`만 | `fw_axis.c`는 가짜 HAL로 직접 |
| `fw_modbus` | TCP 서버(IP101, 192.168.0.220:502), RTU 슬레이브(RS-485 UART1), 모드에 따라 하나만 시작 | 전부 | 문법만 |
| `main` | `app_main`(첫 문장 GPIO33 LOW), `Kconfig.projbuild` | 전부 | 문법만 |

파일 수: C 15개 + 헤더 11개(약 2,900행), CMakeLists 6개, Kconfig 1개, sdkconfig 조각 5개(기본 1 + 모델 4). 모듈(컴포넌트) 5개(`fw_core`, `fw_hal`, `fw_motion`, `fw_modbus`, `main`).

부팅 순서(`app_main` → `fw_motion_start`): GPIO33 LOW(`fw_hal_oe_early_forbid`) → 리셋 원인 읽기 → 태스크 WDT 설정 → NVS 열기·설정 읽기·검증(CRC·버전·범위) → 통신 모드 결정 → 펄스 엔진 생성(축별 MCPWM 그룹·PCNT) → STOP/START 입력 초기화 → **모든 필수 단계가 성공했을 때만 GPIO33 HIGH(허용)** → 운전 태스크 시작 → 통신 시작. 한 단계라도 실패하면 GPIO33은 LOW로 남고, 레지스터(`boot_status` 0x002D, 축 알람)로 이유를 읽을 수 있다. 통신은 운전 금지 상태에서도 시작한다.

## 2. 가져온 출처

| 출처 | 가져온 것 | 바꾼 점 |
|---|---|---|
| servo1axis `motion_core.c` | MCPWM 타이머·오퍼레이터·비교기·제너레이터, PCNT 30,000 블록 누적, 감시점 | 시험 펌웨어를 거쳐 가져옴(아래) |
| servo1axis `movion_modbus_tcp.c` | 레거시 창 의미(HR/코일/DI), 프로파일 계산(`calculate_profile`/`profile_frequency`), 시작 주파수 자동 계산 `min(5000, speed)`, TCP 서버·프레이밍·5 s 소켓 타임아웃, IP101 초기화 | 쓸 때마다 NVS 저장 → 지연·일괄 저장. W5500·사다리(ladder) 제외. DO23/26/27/46을 한꺼번에 켜던 코일 1 → 축별 서보 ON |
| servo1axis `movion_modbus_rtu_master.c` | UART1 + RS-485 반이중(`uart_set_mode`) 설정 패턴 | 마스터 → 슬레이브 구현은 신규 |
| test-fw `tfw_pulse2` | 2축 MCPWM(그룹 = 축 번호)+PCNT, `ceil(res/f)` 주기(요청 초과 금지), IRAM ISR(M-01), STOP 시작 거부(N-03), 핀 중복 검사(M-08) | 시험 전용(탐침, 통계, 디버그 GPIO, 고정 주파수 운전, GPTimer 갱신, 단일 `block_fn`) 제거. 갱신은 **태스크에서**(ISR 안 FPU 금지, 리뷰 M-02). 해상도는 축별 파라미터 |
| test-fw `tfw_estop_io` | 축별 STOP ISR + MCPWM fault brake(경로 B), 래치 후 정지 순서(N-03), 부팅 시 활성 래치(M-04), START 엣지 폴링, 초기화 실패 시 운전 거부(N-02), ISR 우선순위(M-03) | 핀을 제품 값으로(X STOP 19/START 54, Y STOP 8/START 7), 경로 선택·통계 제거(두 경로 항상 사용), 해제는 START 엣지만, 예비 DI 4점 추가 |
| test-fw `tfw_logic` | `tfw_core.c`(프로파일, `tfw_es_*`, `tfw_start_*`) → `fw_profile.c`, `fw_stop.c` | 이름 변경, 수동 시작 속도, 감속 정지 재계획(`fw_profile_plan_stop`), 단일 축 구조체 |

시험 펌웨어의 시험 시퀀서·부하 설정·LA 훅·시험 창(0x7000)·결과 구조체는 가져오지 않았다.

## 3. 구현 요약

- **축:** X(펄스 25/방향 32), Y(펄스 20/방향 21). 공용 출력 인에이블 GPIO33(HIGH=허용). 축마다 위치(펄스 수)·속도·가감속, 방향, 사다리꼴/삼각형 프로파일, 시작 주파수 자동 계산(수동 `start_speed`≠0이면 그 값). 위치는 소프트웨어로 추적(펄스 수 × 방향). `FW_AXIS_COUNT=1`이면 Y 자원(MCPWM 그룹 1, PCNT, STOP/START/DI, DO2)을 만들지 않고 0x2000 영역은 예외 02.
- **STOP(D-058):** NC 접점, 기본 활성 HIGH(`FW_STOP_ACTIVE_LOW=n`). 축별 GPIO ISR(경로 A: 래치 후 해당 축 `force LOW`)과 MCPWM GPIO fault(경로 B)를 모두 쓴다. 다른 축과 GPIO33은 건드리지 않는다. 래치는 **D-072**에 따라 STOP 입력이 10 ms 이상 비활성인 상태의 명시적 시작(물리 START 엣지, HMI/Modbus의 START_ABS/START_REL, 레거시 코일 0)으로 해제되고 그 시작이 실행된다. 해제 검사는 다른 모든 거부 조건(알람·서보 OFF·범위) 뒤에 있어, 거부되는 명령은 래치를 풀지 않는다. STOP 입력 활성 중, 해제 10 ms 미만, MCPWM fault 복구 실패 시에는 0x10 거부 + 래치 유지(복구 실패 시 래치를 다시 세움). `ESTOP_RELEASE`·`ALARM_RESET`은 래치를 풀지 않는다(거부). 누른 채 해제해도 재기동 없음. 래치 설정(ISR, 1 ms 레벨 감시)과 해제는 같은 `portMUX`로 보호. **레벨 감시(M-02):** 1 ms 감시 태스크가 입력 활성인데 래치가 없으면 래치+해당 축 펄스 차단, `fw_axis_poll`도 입력 레벨을 본다. HMI 정지(`STOP` 명령/코일)는 설정 감속 시간으로 감속 정지, `QUICK_STOP`은 즉시.
- **START:** 엣지, 활성 LOW, 5 ms 디바운스, 부팅 시 눌려 있어도 엣지 아님. Kconfig `FW_*_START_GPIO=-1`이면 START 입력 없음. 내부 풀업으로 무입력은 "안 눌림".
- **I/O:** 서보 ON X=DO1(23), Y=DO2(26). DO3(27)/DO4(46)는 `0x0120`(b0/b1)로 제어. 예비 DI 18·17·15·16은 읽어서 `di_raw`(0x0031)로만 노출.
- **Modbus:** interface-spec §5 구현(§4 레지스터 요약 참조). 레거시 창은 X축의 별칭(속도·가감속은 축 영역과 같은 저장소). FC 01/02/03/04/05/06/15/16. 모든 쓰기는 후보 이미지에서 검증한 뒤 한 번에 반영(실패 시 변경·명령 없음).
- **통신 모드(D-056, D-059):** `comm_mode`(0x400E, 0=TCP, 1=RTU)를 NVS에 저장, **재부팅 후 적용**. 출하 기본값은 모델별(D-059): TCP 모델=TCP, `FW_MODEL_RTU=y` 모델=RTU(`fw_cfg_defaults_model`, 첫 부팅·설정 손상·`LOAD_DEFAULT` 모두). 네트워크 워드(0x4000~0x400E) 쓰기는 `unlock_key` 후에만 가능하고(없으면 예외 03, 변경 없음), 네트워크만 바뀐 쓰기는 자동 저장을 요청하지 않는다(`NET_APPLY` 0x400F 또는 `SAVE`로 저장). `FW_MODEL_RTU=n` 모델은 RTU 쓰기를 예외 03으로 거부하고, 저장값이 RTU여도 부팅 때 TCP로 되돌린다(`boot_status` b8). 선택된 쪽만 시작(RTU 모드에서는 이더넷도 올리지 않음).
- **저장:** 레지스터 쓰기는 RAM만 바꾸고 변경이 있을 때만 "미저장" 표시. 마지막 쓰기 2 s 후(`auto_save`=1) 또는 `SAVE` 명령에서 **축이 정지 중일 때만** NVS 한 덩어리(CRC32 + 버전)로 기록. 저장·재부팅 결정은 락 안에서 `s_saving`/`s_reboot_commit`을 세워(M-03) 판단과 실행 사이에 START가 끼어들 수 없다. 저장 중 물리 START는 버리지 않고 보류한다(m-04). 쓰기 실패 시 `E-NVM`(0x0702) 이력 + `sys_status` b5.
- **안전:** 부팅 중 GPIO33 LOW(app_main 첫 문장), 모든 필수 초기화(WDT·NVS·설정 CRC·축별 펄스·STOP 입력)가 성공해야 HIGH. 실패 시 해당 알람(0x0602 펄스 엔진, 0x0701 설정 손상, 0x0702 NVM, STOP 입력 실패는 비상정지로 표시)과 시작 거부 0x11/0x10. 설정 손상은 기본값을 올리고 "SAVE 성공" 전까지 기동 금지. ISR은 IRAM, DRAM 정적 변수만, RTOS API 없음(M-01 계승). 태스크 WDT(운전 태스크만 구독, 1000 ms, 패닉 = 리셋) + 인터럽트 WDT 300 ms. 리셋 원인은 `reset_reason`(0x002C), WDT 리셋이면 이력에 E-WDT(0x0801) 한 건.

## 4. HMI 화면 데이터에 필요한 레지스터 요약

주소는 0 기준 PDU 주소(16진). 32비트는 하위 워드 먼저. B = 0x1000(X), 0x2000(Y). EasyBuilder 표기는 4x-N = 주소 N−1.

| 화면 | 레지스터 | 비고 |
|---|---|---|
| 기존 1축 화면(호환) | HR 0x0000(위치 U32), 0x0002(속도 U32), 0x0006(현재 U32, 읽기), 0x000A(상태), 0x000D(결과), 0x000E(오류), 0x000F(접속 수), 0x0010(링크), 0x0012(시작 속도, 읽기), 0x0014/0x0016(가감속 ms U32). 코일 0 START, 1 서보, 2 강제 정지/리셋, 3 방향. DI 0~4 | 무수정 호환이 목표. 실제 HMI 연동은 미확인 |
| 운전 데이터 | 시스템: 0x0020 모델, 0x0021 축 수, 0x0025 sys_status, 0x0026 estop_state, 0x0028 uptime, 0x002A heartbeat. 축: B+0x000 axis_state, +0x001 axis_status, +0x002 cmd_position S32, +0x004 cmd_speed S32, +0x006 목표 위치 S32, +0x008 alarm_code, +0x009 last_stop_cause, +0x00B cmd_result, +0x00E 이번 이동 펄스 | axis_state 0 서보 OFF / 1 대기 / 2 운전 / 5 감속 정지 / 6 알람 / 7 비상정지 |
| 운전 조작 | B+0x040 axis_command(1 ABS, 2 REL, 3 STOP, 5 ALARM_RESET, 6/7 서보, 11 SET_POSITION, 12 QUICK_STOP), B+0x042 목표 S32, +0x044 속도 U32, +0x046/+0x047 가감속 ms U16. 코일 0x10×n+2 STOP, +3 서보. 0x0100 sys_command(2 SAVE, 3 LOAD_DEFAULT*, 4 REBOOT*, 5 이력 삭제, 6 STOP_ALL), 0x0101 결과, 0x0102 unlock_key(0x4D56, 10 s) | *잠금 필요. HOME(4), JOG(8~10)는 v1 미구현 → 예외 03 + cmd_result 0x17 |
| 파라미터 | B+0x100~0x12B(44워드 한 번에 읽기), 시스템 0x0110~0x0114 | 구현 값만 허용하도록 범위 좁힘(§6) |
| I/O 표시 | 0x0031 di_raw(b0 X START, b1 X STOP, b2 DI3, b3 DI4, b4 Y START, b5 Y STOP, b6 DI7, b7 DI8; 1=활성), 0x0032 do_state(b0 DO1 … b3 DO4), 0x0120 DO3/DO4 쓰기 | 신규 주소 |
| 진단·알람 | 0x002C reset_reason(0 불명, 1 전원, 2 SW, 3 패닉, 4 INT WDT, 5 TASK WDT, 6 기타 WDT, 7 브라운아웃, 8 딥슬립, 9 외부, 10 기타), 0x002D boot_status, 0x002E 실제 통신 모드, 0x0030 alarm_axis_mask, 0x3000 이력 건수, 0x3010+4i 이력(코드, 축, 시각 U32), B+0x200 알람 | boot_status: b0 X 펄스, b1 Y 펄스, b2 STOP 입력, b3 NVS, b4 설정 CRC, b5 WDT, b6 X 축 MCPWM fault 경로 묶임, b7 Y 축 fault 경로 묶임(0이면 그 축은 운전 금지, 알람 0x0602), b8 모드 되돌림, b9 GPIO33 허용 중 |
| 네트워크 | 0x4000 IP U32(0xC0A800DC = 192.168.0.220), 0x4002 마스크, 0x4004 게이트웨이, 0x4006 유닛 ID, 0x4007 MAC 3워드(읽기), 0x400A RTU 보율÷100, 0x400B 패리티(0 없음, 1 짝, 2 홀), 0x400C RTU 주소, 0x400E comm_mode, 0x400F net_apply(1 = 저장, 잠금 필요) | 변경은 재부팅 후 적용 |

알람 코드: 0x0101 비상정지(해당 축), 0x0401 통신두절, 0x0602 펄스 엔진/WDT 초기화 실패, 0x0701 설정 손상, 0x0702 NVM, 0x0801 WDT 리셋 이력.

## 5. 권고 기본값 (CLAUDE.md 규칙 9)

| # | 항목 | 값 | 근거 |
|---|---|---|---|
| R1 | 운전 태스크 주기 | 2 ms(`FW_UPDATE_MS` 1~10) | servo1axis는 10 ms. 1 MHz에서 갱신 간격 2,000펄스, 5 kHz에서 10펄스. 태스크 컨텍스트 계산이라 ISR FPU 문제 없음(리뷰 M-02) |
| R2 | 펄스 타이머 해상도 | 2 MHz(`pulse_res_hz`) | servo1axis 1 MHz 실적, 최저 31 Hz까지 표현(65,535틱). 허용 목록 {2,10,20,40} MHz(T-09 후 확정) |
| R3 | WDT | 태스크 1000 ms(운전 태스크만), 인터럽트 WDT 300 ms | NVS 쓰기·플래시 소거 시간 여유. T-15 실측으로 조정 |
| R4 | START | 활성 LOW, 내부 풀업, 5 ms 디바운스 | test-fw 권고값 계승. 보드 극성은 미실측 |
| R5 | STOP | 활성 HIGH(NC 접점 개방), 해제 안정 10 ms | 기준선 §3, test-fw 계승 |
| R6 | 물리 START 동작 | X: 레거시 START와 동일(`legacy_move_pulses`, 방향 코일). Y: `START_REL`(`target_position`의 부호 = 방향) | D-050은 "엣지로 펄스열 시작"만 정함. 기존 사다리의 동작을 X에 그대로 둠 |
| R7 | `ESTOP_RELEASE`(0x0100=1) | 항상 거부(결과 0x10). 복구는 D-072의 시작 명령 | D-058·D-072: 별도 해제 명령 없음. §7 이슈 1 |
| R8 | 위치 유효성 | 부팅·STOP 트립 후 무효, `require_home`=1이면 ABS 거부(0x16). `SET_POSITION`이 유효로 되돌림, REL은 항상 가능 | HOME 미구현이므로 ABS를 쓸 수 있는 유일한 경로 |
| R9 | 서보 ON | `svon_mode`=0 이동 중 자동 ON/끝나면 OFF, 코일·SERVO_ON 명령은 수동 유지(양 모드). 모드 1은 수동 ON 없으면 START 거부(0x12) | 기준선 §3 DC-18 |
| R10 | 이동 속도 한도 | 레거시 창에서 쓴 값은 100~1,000,000 Hz, 축 영역에서 쓴 값은 `max_speed`(기본 100,000) 이하 | 레거시 우선(기존 1 MHz 실적), 축 영역은 스펙 |
| R11 | 레거시 32비트 반쪽 쓰기 | 레거시 창만 허용(servo1axis 동작), 축·시스템 영역은 예외 03 | 스펙 §5.1 "기존 맵 우선" |
| R12 | `rtu_baud` 인코딩 | 보율 ÷ 100(9600 → 96, 115200 → 1152) | 115200은 U16에 안 들어감(호스트 시험에서 발견). 허용 1200~115200 |
| R13 | RTU 기본값 | 9600, 패리티 없음, 8N1, 주소 1 | servo1axis 설정 계승, 슬레이브 주소는 1(255는 마스터가 부르던 대상 값) |
| R14 | `unlock_key` | 0x4D56, 10 s | 스펙 "고정 키값 [가정]" |
| R15 | 통신두절 | 기본 사용 안 함, 1000 ms, 범위 0(모든 접속 무요청) / 1(마지막 명령 연결 무요청 또는 끊김). 동작은 감속 정지 + 알람 0x0401(`ALARM_RESET`으로 해제) | 물리 START 운전을 HMI 없이도 쓸 수 있어야 함 |
| R16 | `REBOOT` | 저장 대기 변경이 있으면 먼저 저장 후 재시작, 응답 후 300 ms | HMI 설정 손실 방지 |
| R17 | 최대 접속 수 | 4(범위 1~8), 부팅 시 반영 | 스펙 [가정], T-05는 8 빌드 |
| R18 | `default_accel_ms`/`default_decel_ms` | 저장·읽기만 하고 동작에 쓰지 않음. 이동의 가감속은 `accel_time_ms`/`decel_time_ms`, HMI 정지의 감속도 같은 `decel_time_ms` | 스펙의 "다음 이동" 의미가 불명확 |
| R19 | 이더넷 | RTU 모드에서는 올리지 않음 | "TCP와 RTU 중 하나만 동작" |
| R20 | MCPWM 주기 갱신 | 타이머 비움(TEZ) 시점. `FW_PERIOD_IMMEDIATE=y`로 servo1axis 방식(즉시) 전환 | 리뷰 M-05 미확인. 첫 사이클은 HIL로 확인 |
| R21 | 알람 이력 | RAM만(32건, 재부팅 시 지워짐). 부팅 시 WDT 리셋이면 한 건 기록 | NVS 쓰기 최소화 |

## 6. 설계서와 다른 점 / 구현 범위

- **v1 미구현(스펙 §5에는 있음):** HOME, JOG, 리밋 입력, 소프트 리밋, S자 곡선, CW/CCW, 펄스 논리 반전, INP 대기, 위치 유지 정책, 단독 운전(0x6000), 펌웨어 갱신 창(0x5000, USB로 갱신 D-037). 해당 명령은 예외 03(+`cmd_result` 0x17), 해당 파라미터는 구현 값(0)만 허용해 HMI가 동작을 오해하지 않게 했다. 홈·리밋·`input_logic` 등 나머지 파라미터는 저장·읽기만 한다. 0x5000, 0x6000은 예외 02.
- **스펙에 없는 신규 주소:** 0x002C reset_reason, 0x002D boot_status, 0x002E 실제 통신 모드, 0x0031 di_raw, 0x0032 do_state, 0x0120 DO3/DO4, 0x400E comm_mode. `regmap_version`은 2 그대로 둠 → 맵 문서(`regmap.yaml`) 갱신 필요.
- `alarm_axis_mask`는 비상정지 중에도 해당 축 비트를 세운다. `sys_inputs`(0x002B) b1은 "게이트 되읽기"가 아니라 우리가 구동한 GPIO33 상태(읽기 회로 없음).
- 레거시 `legacy_move_pulses`가 2,147,483,647을 넘으면 START가 `cmd_result` 0x17, 레거시 오류 1(스펙 §5.0.1).
- 알람 이력 `hist_latest_index`는 항상 0(0번이 최신).

## 7. 이슈 제기

1. **(종결, D-072 반영)** 시작 명령이 래치를 푼다. 남은 문서 불일치(메인 세션): interface-spec §5.3 `estop_state`=2 "해제 명령 대기", sys_command 1 ESTOP_RELEASE, §5.8 0x0101 "입력 해제 + ESTOP_RELEASE"는 D-072와 맞지 않는다.
2. **물리 START의 Y축 의미(R6)**와 서보 ON 선행 시간: 스펙에 서보 ON 후 펄스 출력까지의 지연이 없다(많은 드라이브가 수십 ms 필요). 현재 0 ms. 드라이브 사양 확인 후 파라미터 추가 여부 결정.
3. **보드:** 기준선 §4의 축별 AND 게이트(D-070)가 없으면 STOP 하드웨어 차단은 MCPWM fault 경로에 달려 있다. 이 경로의 IDF 동작(아래 §8)이 확인되기 전에는 소프트웨어 ISR 경로가 유일한 보증이다.
4. **스펙 보완:** `rtu_baud` 인코딩(R12), 신규 주소 7개(§6), `default_accel/decel_ms` 의미(R18), 위치 무효·`require_home` 기본 1에서 ABS 사용 절차(R8).
5. **F-14 USB 콘솔:** `sdkconfig.defaults`는 servo1axis와 같은 콘솔 설정(UART 기본 + USB Serial/JTAG 보조)이다. UART 콘솔 핀과 RS-485(24/47/48)·펄스 핀이 겹치지 않는지는 보드 결선 확인 전까지 미확인.
6. 설정 이미지 레이아웃(`fw_cfg_t`)이 바뀌면 저장된 설정은 크기·버전 불일치로 E-PARAM(기동 금지, SAVE 필요)이 된다. 펌웨어 갱신 시 정책(자동 이전 여부)은 정해지지 않았다.

## 8. 미검증 항목

| # | 항목 | 이유 / 확인 방법 |
|---|---|---|
| U1 | **IDF 5.5.3 빌드 전체** | 컨테이너에 ESP-IDF 없음. `idf_stub`은 API를 기억으로 쓴 것이라 서명 일치를 증명하지 못한다. 첫 빌드에서 컴파일 오류가 날 수 있다 |
| U2 | `sdkconfig.defaults`의 IRAM 옵션 이름(`CONFIG_MCPWM_ISR_HANDLER_IN_IRAM` 등), 인터럽트 우선순위 적용 | `grep IRAM sdkconfig`로 확인. 서비스가 이미 설치돼 있으면 GPIO ISR 레벨·IRAM 플래그가 적용되지 않음(경고 로그) |
| U3 | MCPWM fault brake(OST) 설정·`recover_from_fault`, 한 GPIO를 fault와 GPIO ISR에 동시 사용 | test-fw TFW-04와 같은 열린 질문. T-07 |
| U4 | 정지 상태에서 쓴 주기가 시작 첫 사이클에 적용되는지(리뷰 M-05) | 첫 HIL 항목: "정지 → 다른 주파수로 시작" 직후 2~3주기를 LA로 확인. 문제면 `FW_PERIOD_IMMEDIATE=y` |
| U5 | 2축 동시 200 kpps 이상 펄스 수 정확도, 목표 정지 ISR 지연, 속도 갱신 경계 | T-03, T-04, T-08, T-09 실측. 2 ms 갱신 지연은 측정 전 |
| U6 | NVS 쓰기 중 펄스 ISR 간섭 | T-06. 설계상 정지 중에만 쓰지만 IRAM 옵션 확인 필요 |
| U7 | WDT 리셋까지 시간·출력 지속(T-15), 리셋 직후 GPIO33 | 실측. 리셋 원인 레지스터는 구현됨 |
| U8 | RTU 프레임 간격(3.5문자, 1 ms 틱 해상도), 반이중 DE 타이밍, 실제 마스터 호환 | 장비 필요. CRC는 호스트에서 검증 |
| U9 | IP101/EMAC 핀 설정(P4 기본값), 링크 이벤트 | servo1axis 코드를 옮김, 실보드 미확인 |
| U10 | TLP293 극성(START 활성 LOW 가정, STOP 활성 HIGH), DO 극성(HIGH=ON), 예비 DI 극성 | 보드 실측 |
| U11 | 실제 HMI(이지뷰, M2I)에서 레거시 창·32비트 하위 워드 우선 동작 | M2I 설정 가능 여부는 스펙 이슈 9 |
| U12 | 부트로더~app_main 사이 GPIO33 | 외부 10 kΩ 풀다운(R32, r2 반영)에 의존. 펌웨어는 app_main 첫 문장에서만 보장 |
| U13 | 실모터·드라이브 운전, 1 MHz 2축 | 사용자 장비 |

## 9. 빌드·플래시 방법 (사용자 PC에서; 에이전트는 실행하지 않음)

```
. $IDF_PATH/export.sh                      # ESP-IDF 5.5.3
cd project/04_code/firmware
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.model_2axis_tcp" set-target esp32p4
idf.py menuconfig                          # "Pulse servo controller" 메뉴: 핀, 극성, 우선순위 확인
idf.py build
idf.py -p <포트> flash monitor             # 플래시는 사용자가 실행
```

D-059에 따라 RTU 모델의 출하 기본 통신 모드는 RTU다. 모델 4종: `sdkconfig.model_{1axis,2axis}_{tcp,rtu}`. 출하 기본 통신 모드는 TCP 모델=TCP, RTU 모델=RTU(D-059). 변경은 잠금 해제 후 `0x400E` 쓰기 + `0x400F`=1 + 재부팅. 첫 부팅 로그에서 `boot_status`, `reset reason`, STOP 레벨(`ACTIVE at boot`가 없는지)을 확인한다. 첫 HIL 순서 권고: GPIO33 LOW 확인(전원 투입 직후) → U4 → STOP 응답(T-07) → 단축 이동 펄스 수 → 2축 동시.

## 10. 호스트 시험 결과 (그대로)

리뷰 1차 반영 후 재실행(2026-10-31, `make clean` 후): `make run` = test_core 8,689 / test_regmap 459 / test_axis 259 checks, 0 failures; pytest 32 passed. 반영 전: 8,689/424/204, 31 passed. 아래는 항목별 설명.

실행: `make -C project/04_code/tests/firmware run` 과 `cd project/04_code/tests/firmware && python3 -I -m pytest -q test_cfg_syntax.py` (2026-10-31, gcc, Python 3.13).

| 시험 | 대상 | 결과 |
|---|---|---|
| `test_core.c` 23개 시험 | 주기 계산(요청 초과 없음, 저주파 거부), 시작 속도, 사다리꼴(1 MHz 실적 값), 삼각형, 감속 정지 재계획, CRC, STOP/START 상태 기계(래치·해제·무시·누른 채 해제·디바운스·부팅 시 활성·축 독립), 파라미터 범위 경계·교차 규칙·네트워크 검증, 모드 선택, 설정 무결성, 지연 저장, 통신두절, 핀 중복, 리셋 원인 | 8,689 checks, **0 failures** |
| `test_regmap.c` 16개 시험 | 레거시 창 읽기·쓰기·코일·DI, 시스템 영역, 축 영역 배치(S32, 하위 워드 우선), 1축 모델의 축2 예외, 원자성(실패 시 변경·명령 없음), 명령 라우팅, 미구현 명령, 변경 알림(같은 값 재쓰기는 저장 요청 없음), 네트워크·모드(TCP 모델은 RTU 거부), 이력, 프레이밍·예외 | 459 checks, **0 failures** |
| `test_axis.c` 19개 시험 (가짜 HAL) | 레거시 이동·방향·dir_invert, 펄스 불일치, 한도, ABS/REL/위치, 감속 정지, 즉시 정지, 축별 STOP 래치와 START 엣지 재기동, 부팅 시 STOP, Y 물리 START, 서보 모드, 초기화 실패 4종(펄스·설정·STOP 입력·WDT·NVS), 통신두절 정지·알람 해제, 프로파일 갱신 실패, 1축 빌드 | 259 checks, **0 failures** |
| `test_cfg_syntax.py` (pytest) | 전체 C 파일을 IDF 스텁에 대해 `gcc -fsyntax-only`, sdkconfig 조합 9종 + 모델 조각 4종(bool n은 정의하지 않는 IDF 규칙), `fw_cfg.h` 매크로 정적 단언, 음성 대조(미정의 bool을 값으로 쓰면 실패), bool은 `#if` 줄에만, 쓰인 CONFIG 전부 Kconfig 선언, 기본 핀이 기준선 신호표와 일치·중복 없음, `sdkconfig.defaults` 형식과 안전 옵션, `app_main` 첫 호출이 `fw_hal_oe_early_forbid` | 32 passed |

개발 중 시험이 잡은 것(수정 후 재실행, 시험을 지우지 않음): ① `rtu_baud` 115200이 U16에 안 들어감 → ÷100 인코딩(R12). ② 감속 정지 중 상태가 알람보다 우선해야 하는데 반대였음(`fw_axis.c` 수정). 시험 쪽 수정: 업타임 상위 워드와 부분 쓰기 기대값 오류, 감속 정지 종료 주파수 검사는 시험의 펄스 간격(200)이 거칠어 20펄스 간격 + 12 kHz 이하로 바꿨다(실제 2 ms 갱신은 5 kHz에서 약 10펄스).

**이 시험이 증명하지 않는 것:** IDF API 서명, 인터럽트·IRAM 동작, 타이밍, MCPWM/PCNT 실동작, NVS 실제 기록, 네트워크·UART, 실장비 전부(§8).

## 11. 리뷰 1차(firmware-review-1) 지적별 반영 표 (2026-10-31)

판정: 조건부 통과(Major 7 = M-01~M-06 + T-01, Minor 10). 아래 "반영" = 코드와 호스트 시험으로 반영. IDF 동작은 호스트에서 증명할 수 없으므로 HIL 확인 항목을 따로 적었다.

| ID | 처리 | 내용 | 시험 / 남은 확인 |
|---|---|---|---|
| M-01 | 반영 | `fw_hal_stop_release()` 신설(hal_io.c, STOP ISR과 같은 `portMUX`). `start_move()`에서 STOP 입력 활성·io 미초기화는 즉시 0x10, 래치만 남은 경우는 다른 모든 거부 조건 뒤에서 해제 후 시작. recover 실패 시 래치 재설정+0x10. io_task의 물리 START 경로도 같은 mux로 감쌈. ESTOP_RELEASE 거부 유지. 주석·notes R7·§7 이슈 1 갱신 | `test_d072_hmi_start_releases_latch`(START_REL, 레거시 코일 0, START_ABS), `test_d072_refusals_keep_latch`(<10 ms, 입력 활성, 서보 OFF·범위·알람 거부 시 래치 유지). HIL: 실제 ISR vs 해제 경쟁 |
| M-02 | 반영 | `fw_es_level_guard()`(fw_stop.h) + poll_once가 입력 활성·래치 없음이면 래치와 해당 축 차단. `fw_axis_poll()`은 `latched \|\| stop_input` | `test_m02_level_backup_without_isr`(fake_hal `FAKE_NO_ISR`: 1 ms 내 정지·래치, 축 poll만으로도 정지). HIL: T-07 |
| M-03 | 반영 | 저장·재부팅 결정과 같은 락 구간에서 `s_saving`/`s_reboot_commit` 설정, 시작 명령(HMI, 레거시, 물리) 모두 `start_blocked()` 확인 | **호스트 시험 없음**(fw_motion.c는 IDF 의존). review 허용안 "T-06에 저장 직전 START" HIL 항목으로 넘김 |
| M-04 | 반영 | `fw_cfg_defaults_model()`; `fw_dev_init`(첫 부팅·손상)과 `LOAD_DEFAULT`가 사용 | `test_model_default_comm_mode`, `test_network_and_mode`(RTU 모델 0x400E 기본 1) |
| M-05 | 반영 (권고안 병행) | 네트워크 워드 변경은 `unlock_key` 없으면 예외 03(변경 없음). 네트워크만 바뀐 쓰기는 `cfg_changed` 미호출, 저장은 `NET_APPLY`/`SAVE`. 같은 쓰기 PDU에서 unlock과 네트워크를 함께 쓰면 잠금 해제는 쓰기 후 적용이라 거부됨(별도 쓰기 필요) | `test_network_and_mode`(잠금 없이 0x4000/0x4006/0x400E 거부, 해제 후 쓰기에서 `cfg_changed_n`=0) |
| M-06 | 반영 | `boot_status` b6/b7(fault 경로 묶임). 묶이지 않은 축은 `engine_allowed`=false, 알람 0x0602, 시작 0x11. [권고] 허용 Kconfig는 만들지 않음(T-07 확인 전 소프트웨어 경로만 운전은 기준선 §3 의도와 다름). recover 실패는 M-01로 처리. "정체 감시"(계수 증가 없음 → 정지)는 **미구현**: 설계에 없는 임계값이라 T-07 결과를 보고 결정 | `test_m06_fault_path`(recover 실패 → 0x10·래치 유지, X 미바인딩 → X만 금지, Y 정상) |
| T-01 | 반영 | `test_axis.c` 이전 단언("HMI도 재기동 불가") 삭제하고 D-072 시험으로 교체. 위 시험 목록 참조. M-03만 호스트 증명 불가(위) | make run 0 failures |
| m-01 | 미반영 | TWDT ISR 훅·시험 빌드 정지 유도는 IDF 심볼·T-15 빌드 필요. HIL 후 결정 | U7 |
| m-02 | 일부 | `fw_hal_wdt_add_self()`가 int 반환, 실패하면 `FW_BOOT_WDT` 해제+GPIO33 LOW. io_task 감시는 미반영 | HIL |
| m-03 | 미반영 | T-03/T-04에서 LA로 마지막 펄스 폭 확인 후 결정 | HIL |
| m-04 | 반영 | start_cb를 `fw_hal_io_init` 전에 등록, 저장 중 물리 START는 보류 | 호스트 시험 없음(fw_motion.c) |
| m-05 | 반영 | STOP ISR 서비스 `ESP_INTR_FLAG_IRAM` 무조건 | pytest `test_stop_isr_iram_flag_is_unconditional_and_mux_protected` |
| m-06 | 반영 | 어느 축에도 COMMLOSS 알람이 없으면 `s_cl_tripped` 해제 | 호스트 시험 없음(fw_motion.c) |
| m-07 | 미반영 | RTU t3.5 이벤트 기반은 HIL 필요(U8) | U8 |
| m-08 | 반영 | 접속 수를 accept 루프에서 증가, 태스크 생성 실패 시 감소 | 호스트 시험 없음 |
| m-09 | 반영 | FC01/02 개수 0 또는 2000 초과 → 03, 정의 범위 밖 → 02. 레거시 창 호환 기준은 servo1axis(`COIL_COUNT` 기준 예외 02)와 같다 | `test_read_bits_limits` |
| m-10 | 반영(문서) | 아래 §12 | – |

## 12. 신규 레지스터(스펙 밖) 7개와 0x400E 잠금 조건

`regmap_version`=2 유지(기존 주소 불변, 추가만). 메인 세션이 `regmap.yaml`과 interface-spec §5.3·§5.10에 올려야 한다.

| 주소 | 이름 | 접근 | 내용 |
|---|---|---|---|
| 0x002C | reset_reason | R | 0 불명 1 전원 2 SW 3 패닉 4 INT WDT 5 TASK WDT 6 기타 WDT 7 브라운아웃 8 딥슬립 9 외부 10 기타 (T-15) |
| 0x002D | boot_status | R | b0 X 펄스, b1 Y 펄스, b2 STOP 입력, b3 NVS, b4 설정 CRC, b5 WDT, **b6 X fault 경로, b7 Y fault 경로(리뷰 M-06 신규)**, b8 모드 되돌림, b9 GPIO33 허용 중 |
| 0x002E | 실제 통신 모드 | R | 0 TCP / 1 RTU (재부팅 후 적용된 값) |
| 0x0031 | di_raw | R | b0 X START, b1 X STOP, b2 DI3, b3 DI4, b4 Y START, b5 Y STOP, b6 DI7, b7 DI8, 1=활성. 예비 DI 극성은 START 극성을 그대로 쓴다: **보드 실측 전 "미확인"**(U10) |
| 0x0032 | do_state | R | b0 DO1 … b3 DO4 (구동값) |
| 0x0120 | user_do | R/W | b0 DO3, b1 DO4 (명령 영역, 저장 안 함) |
| 0x400E | comm_mode | R/W | 0 TCP / 1 RTU. RTU는 RS-485 실장 모델(`FW_MODEL_RTU=y`)에서만 허용(그 외 예외 03). 출하 기본값: TCP 모델=TCP, RTU 모델=RTU (D-059). **재부팅 후 적용** |

**0x400E(및 0x4000~0x400D 네트워크 워드) 잠금 조건:**
1. 먼저 `unlock_key`(0x0102)=0x4D56을 별도 쓰기로 보내 10 s 안에 쓴다. 잠금 상태에서 값이 바뀌는 쓰기는 예외 03이고 아무것도 바뀌지 않는다(같은 값 재쓰기는 허용).
2. 쓰기만으로는 NVS 저장이 요청되지 않는다(자동 저장 없음). `net_apply`(0x400F)=1(잠금 필요, 운전 중 거부 0x12) 또는 `SAVE`로 저장한다.
3. 적용은 재부팅 후(`REBOOT`, 잠금 필요).

## 13. 초보자 사용성 기능 FR-029~FR-035 (2026-11-01, D-074~D-076, interface-spec §5.12)

### 13.1 구현 요약
| FR | 구현 위치 | 내용 |
|---|---|---|
| 029 | `fw_core/fw_unit.c/.h`(순수 함수), `fw_params.c`(+0x140~, op +0x4A~), `fw_axis.c`(`cmd_start_unit`), `fw_regmap.c`(position_unit) | 정수 연산만(riscv32에는 `__int128`이 없어 64×64→128 곱셈과 128/64 나눗셈을 직접 구현). 반올림 후 S32/U32 초과·0 나눗셈 검사. 명령 13/14 |
| 030 | `fw_regmap.c` | `setup_done`(0x0130, 저장 이미지), `max_rpm_calc`(축 +0x032) |
| 031 | `fw_regmap.c::fw_wiring_status` | 기존 `di_raw`, `do_state`, `link`, 축별 `loop_seen`(PCNT 계수, `fw_axis.c`), RTU 마지막 유효 프레임(`fw_dev_note_rtu_rx`, `fw_mb_rtu.c`가 호출) 사용 |
| 032 | `fw_axis.c::cmd_test` + `fw_axis_poll` | 유한 이동(INT32_MAX 펄스)을 test_rpm 속도로 시작, 같은 명령 재쓰기 = keepalive, 500 ms 초과 시 `begin_decel` |
| 033 | `fw_regmap.c::fw_alarm_hint` | 우선순위는 spec §5.12 구현 확정 표 |
| 034 | `fw_params.h/.c`(cfg.pt[축][8][8워드]), `fw_regmap.c`(0x6000~), `fw_axis.c` | 포인트 저장·검증·POINT_GO·POINT_TEACH |
| 035 | `fw_regmap.c::fw_sys_reset_cmd`(순수), `fw_params.c`(`fw_cfg_net_reset`, `fw_nrhold_*`), `fw_motion.c` | sys_command 7·8, 부팅 10 s 보유 감지 후 net 복원·저장·재부팅 |

### 13.2 레지스터 변경 여부
**주소 충돌 없음, 이동 없음.** §5.12 배정을 그대로 사용했다. 확인한 점: 축 +0x140~0x147은 par 이미지(0x12C~0x13F 미사용)와 겹치지 않고, +0x04A~0x04E는 op 이미지(0x040~0x04F)의 빈 칸, +0x030/0x032는 축 데이터 빈 칸, 0x0033/0x0034/0x0130은 시스템 블록 빈 칸(0x0130은 0x0120 user_do와 별도 항목), 0x6000~0x60FF는 새 영역. 배정표에 번호가 없던 sys_command 값만 7 = FACTORY_RESET, 8 = NET_RESET으로 정했다. 구현 확정 사항을 interface-spec §5.12 끝에 추가했다(주소 표는 수정하지 않음).

### 13.3 설계와 다른 점 / [권고] 기본값
1. **저장 이미지 버전 1 → 2.** setup_done과 포인트가 이미지에 들어가 구조체 크기가 바뀐다. 이전 이미지는 거부되어 기본값으로 시작(개발 단계라 이전 이미지가 없다고 [가정]).
2. unit_mode 0(펄스)에서 변환은 항등, START_*_UNIT는 0x17 [권고]: 펄스 모드 사용자가 단위 명령을 쓸 이유가 없고 오해를 막는다.
3. 단위 속도가 `speed` 파라미터와 같은 한계(100 Hz 이상, max_speed 이하)를 따르게 했다. 설계서는 "1 MHz 또는 max_speed 초과 시 0x17"만 적었고 하한은 저장 속도 범위와 맞춘 것 [권고].
4. wiring_status b1을 "START 눌리지 않음"으로 해석했다(설계서 문구 "START 대기"). 눌림 확인용 비트가 필요하면 설계 변경 사항이다(이슈 1).
5. b2는 "마지막 이동에서 PCNT가 센 펄스가 있고 계수 불일치가 없음"이다. 이동 전에는 0이다. 실제 펄스 핀 단선은 이 방식으로 이동하기 전에는 알 수 없다(이슈 2).
6. 하드웨어 네트워크 초기화의 "부팅 직후"를 "`fw_motion_start` 끝의 첫 샘플부터 끊김 없이 10 s"로 구현했다. START 입력 디바운스 없이 `di_raw` 원시값을 쓴다.
7. TEST 이동량은 INT32_MAX 펄스로 한정했다(약 2^31 펄스). 최고 1 MHz에서도 35분 이상이라 실제로 끝나지 않는다.
8. 이 구현에서 `target_unit`/`speed_unit`/`point_index`는 op 이미지라 저장 대상이 된다(기존 target/speed와 같은 취급).

### 13.4 시험 결과 (그대로, 2026-11-01)
기존 시험의 기대값 3곳은 §5.12가 이전의 "미정의" 값을 정의했기 때문에 바꿨다(삭제 아님): `test_regmap.c` sys_command 7 → 9, 주소 0x6000 → 0x6100, axis_command 13 → 19의 "거부" 확인.

`make -C project/04_code/tests/firmware run`:
```
test_core.c: 8689 checks, 0 failures
test_regmap.c: 459 checks, 0 failures
test_axis.c: 259 checks, 0 failures
test_beginner.c: 148838 checks, 0 failures   (신규 12개 시험)
```
`make pytest`(test_cfg_syntax.py, 전체 C 파일 gcc -fsyntax-only 포함, 새 파일·`fw_motion.c`·`fw_mb_rtu.c` 변경분 포함): **32 passed**.

개발 중 실패한 것(수정 후 재실행): 시험 쪽 오류만 — max_speed를 jog_max보다 낮게 쓰는 교차 규칙 위반 3건(시험 순서 수정), POINT_GO 가감속 검증용 속도가 시작 속도 이하라 램프가 없는 값이었음(8000으로 변경). 펌웨어 쪽 실패는 없었다.

| 시험 (test_beginner.c) | 다룬 것 |
|---|---|
| unit_basic_and_rounding | mm/°/기어비/펄스 모드 변환, 반올림(+/- 대칭), 속도 변환 |
| unit_boundaries_and_overflow | S32 경계 ±1, 최악 파라미터(1e7 ppr × 1000 기어) 128비트 중간값, U32 속도 초과, 0 나눗셈 방지, 포화 |
| unit_roundtrip | 10개 파라미터 조합 × 4,857 값: 단위→펄스 오차 ≤ 0.5 펄스(double 대조), 펄스→단위→펄스 ≤ 1 펄스(단위가 펄스보다 잘 때), 단위→펄스→단위 ≤ 1 단위(펄스가 단위보다 잘 때). 최대 오차 0 |
| unit_commands | Modbus 경로 REL/ABS_UNIT, position_unit, 속도 초과·하한·1 MHz 초과·목표 오버플로 0x17, 펄스 모드 거부, 파라미터 범위 예외 03, 기본값, 축 독립 |
| setup_done_and_max_rpm | 0/1, 저장 알림, 이미지 검증, max_rpm 계산·65535 포화·0 |
| wiring_status | 비트별(b0~b3, 두 축, b8, b9 시간 경계 3000/3001 ms, b15 무효, 1축), 읽기 전용 |
| alarm_hint | 우선순위 조합, 실제 STOP 입력 연동 |
| test_hold | 속도, 400 ms keepalive 유지, 500 ms 경계(유지)/501 ms(감속 정지), 정지 중 keepalive 불가(0x13), 반대 방향 0x13, STOP 명령, STOP 활성 중 0x10(keepalive 포함), 운전 중 STOP 입력, 속도 범위 0x17 |
| points | 쓰기·읽기, 검증(원자적), 예약 워드 02, GO(가감속 값 확인), 빈 점 0x17, 속도 초과 0x17, TEACH(값·거부), 저장 이미지 포함·검증, Y축·1축 모델 |
| resets | 잠금 0x11, 잠금 만료, 운전 중 0x12, NET_RESET 범위(다른 설정 유지), 잠금 소모, FACTORY_RESET 전체, 모델별 기본 모드 |
| boot_hold | 10 s 경계(9999 ms 미발동, 10000 ms 발동, 1회), 중간 해제 시 영구 취소, 한 입력만, ms 카운터 랩, net 복원 범위 |
| regression_defaults | 새 레지스터 미사용 시 기존 기본값·이미지 검증 유지 |

**이 시험이 증명하지 않는 것:** `fw_motion.c`의 실제 부팅 시퀀스·저장·재부팅(IDF 의존, `-fsyntax-only`만 통과), 실제 START 입력 극성·디바운스, 장시간 TEST 이동의 PCNT 오버플로(HIL), 실장비.

### 13.5 이슈 제기
1. (설계) wiring_status b1의 의미: "START 대기"를 눌림 없음으로 구현. HMI가 "START를 눌러 보세요"를 확인하려면 눌림 래치 비트가 필요하다. 설계 변경이므로 메인 세션 판단.
2. (설계) b2(펄스 루프백)는 첫 이동 전에는 0이다. 마법사 점검 순서에 "TEST_POS 1회" 단계가 필요하다.
3. (HIL) INT32_MAX 펄스 TEST 이동에서 PCNT 누적(`fw_total_count`)과 `fw_profile_calc` float 정밀도가 2^31 펄스 규모에서 유지되는지 확인되지 않았다. 호스트 시험은 감속 정지까지(수천 펄스)만 본다.
4. (설계) FACTORY_RESET은 `par`의 `pulse_res`도 기본값으로 되돌리나, 분해능은 부팅 시에만 적용되므로 REBOOT가 필요하다. HMI 안내 문구에 재부팅을 포함할 것.

### 13.6 PC 도구 정합 (메인 세션 전달, 2026-11-01)
- sys_command: **FACTORY_RESET = 7, NET_RESET = 8** (펌웨어 값과 PC 도구 값이 같다. 변경 없음).
- 포인트 테이블 0x6000~은 v1에서 "단독 운전 순서(FR-013)" 영역을 대체한다(같은 영역을 포인트 테이블로 사용, 순서 운전 기능은 v1 펌웨어에 없다).
- 알람 **0x0603 E-PE-FAULTPATH** 추가(`FW_AL_PE_FAULTPATH`): 축의 MCPWM fault 경로가 묶이지 않았을 때(`boot_status` b6/b7 = 0) 이전의 0x0602 대신 보고한다. 운전 금지 동작은 그대로. alarm_hint 우선순위는 0x0602와 같은 단계(0x0101 > 0x0602 = 0x0603 > 0x0701 …). 기존 시험 `test_axis.c` 343행의 기대 코드를 0x0602 → 0x0603으로 바꿨다(삭제 아님). 펄스 엔진 미생성·WDT 미구독은 0x0602 유지.
