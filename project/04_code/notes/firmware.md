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
- **STOP(D-058):** NC 접점, 기본 활성 HIGH(`FW_STOP_ACTIVE_LOW=n`). 축별 GPIO ISR(경로 A: 래치 후 해당 축 `force LOW`)과 MCPWM GPIO fault(경로 B)를 모두 쓴다. 다른 축과 GPIO33은 건드리지 않는다. 래치는 **START 엣지로만** 해제(입력이 10 ms 이상 비활성일 때). STOP 활성·래치 중 START(물리/HMI)는 거부, 누른 채 해제해도 재기동 없음. HMI 정지(`STOP` 명령/코일)는 설정 감속 시간으로 감속 정지, `QUICK_STOP`은 즉시.
- **START:** 엣지, 활성 LOW, 5 ms 디바운스, 부팅 시 눌려 있어도 엣지 아님. Kconfig `FW_*_START_GPIO=-1`이면 START 입력 없음. 내부 풀업으로 무입력은 "안 눌림".
- **I/O:** 서보 ON X=DO1(23), Y=DO2(26). DO3(27)/DO4(46)는 `0x0120`(b0/b1)로 제어. 예비 DI 18·17·15·16은 읽어서 `di_raw`(0x0031)로만 노출.
- **Modbus:** interface-spec §5 구현(§4 레지스터 요약 참조). 레거시 창은 X축의 별칭(속도·가감속은 축 영역과 같은 저장소). FC 01/02/03/04/05/06/15/16. 모든 쓰기는 후보 이미지에서 검증한 뒤 한 번에 반영(실패 시 변경·명령 없음).
- **통신 모드(D-056, D-059):** `comm_mode`(0x400E, 0=TCP, 1=RTU)를 NVS에 저장, **재부팅 후 적용**. 출하 기본값 TCP. `FW_MODEL_RTU=n` 모델은 RTU 쓰기를 예외 03으로 거부하고, 저장값이 RTU여도 부팅 때 TCP로 되돌린다(`boot_status` b8). 선택된 쪽만 시작(RTU 모드에서는 이더넷도 올리지 않음).
- **저장:** 레지스터 쓰기는 RAM만 바꾸고 변경이 있을 때만 "미저장" 표시. 마지막 쓰기 2 s 후(`auto_save`=1) 또는 `SAVE` 명령에서 **축이 정지 중일 때만** NVS 한 덩어리(CRC32 + 버전)로 기록. 저장 중 START 거부. 쓰기 실패 시 `E-NVM`(0x0702) 이력 + `sys_status` b5.
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
| 진단·알람 | 0x002C reset_reason(0 불명, 1 전원, 2 SW, 3 패닉, 4 INT WDT, 5 TASK WDT, 6 기타 WDT, 7 브라운아웃, 8 딥슬립, 9 외부, 10 기타), 0x002D boot_status, 0x002E 실제 통신 모드, 0x0030 alarm_axis_mask, 0x3000 이력 건수, 0x3010+4i 이력(코드, 축, 시각 U32), B+0x200 알람 | boot_status: b0 X 펄스, b1 Y 펄스, b2 STOP 입력, b3 NVS, b4 설정 CRC, b5 WDT, b8 모드 되돌림, b9 GPIO33 허용 중 |
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
| R7 | `ESTOP_RELEASE`(0x0100=1) | 항상 거부(결과 0x10) | D-058 "래치·리셋 명령 없음"이 interface-spec FR-010보다 우선(기준선 §3). §7 이슈 1 |
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

1. **D-058 vs interface-spec FR-010:** 스펙은 `ESTOP_RELEASE`(sys_command 1)를 두지만 기준선은 "래치·리셋 명령 없음, START 엣지로만"이다. 기준선을 따라 거부로 구현했다. START 입력이 배선되지 않은 장치는 STOP 래치 후 재부팅 외에 복구 수단이 없다 → 설계 확인 필요(HMI 해제를 허용할지).
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

모델 4종: `sdkconfig.model_{1axis,2axis}_{tcp,rtu}`. 출하 기본 통신 모드는 모델과 무관하게 TCP(RTU는 RS-485 실장 모델에서 `0x400E`=1 + 재부팅). 첫 부팅 로그에서 `boot_status`, `reset reason`, STOP 레벨(`ACTIVE at boot`가 없는지)을 확인한다. 첫 HIL 순서 권고: GPIO33 LOW 확인(전원 투입 직후) → U4 → STOP 응답(T-07) → 단축 이동 펄스 수 → 2축 동시.

## 10. 호스트 시험 결과 (그대로)

실행: `make -C project/04_code/tests/firmware run` 과 `cd project/04_code/tests/firmware && python3 -I -m pytest -q test_cfg_syntax.py` (2026-10-31, gcc, Python 3.13).

| 시험 | 대상 | 결과 |
|---|---|---|
| `test_core.c` 23개 시험 | 주기 계산(요청 초과 없음, 저주파 거부), 시작 속도, 사다리꼴(1 MHz 실적 값), 삼각형, 감속 정지 재계획, CRC, STOP/START 상태 기계(래치·해제·무시·누른 채 해제·디바운스·부팅 시 활성·축 독립), 파라미터 범위 경계·교차 규칙·네트워크 검증, 모드 선택, 설정 무결성, 지연 저장, 통신두절, 핀 중복, 리셋 원인 | 8,689 checks, **0 failures** |
| `test_regmap.c` 14개 시험 | 레거시 창 읽기·쓰기·코일·DI, 시스템 영역, 축 영역 배치(S32, 하위 워드 우선), 1축 모델의 축2 예외, 원자성(실패 시 변경·명령 없음), 명령 라우팅, 미구현 명령, 변경 알림(같은 값 재쓰기는 저장 요청 없음), 네트워크·모드(TCP 모델은 RTU 거부), 이력, 프레이밍·예외 | 424 checks, **0 failures** |
| `test_axis.c` 15개 시험 (가짜 HAL) | 레거시 이동·방향·dir_invert, 펄스 불일치, 한도, ABS/REL/위치, 감속 정지, 즉시 정지, 축별 STOP 래치와 START 엣지 재기동, 부팅 시 STOP, Y 물리 START, 서보 모드, 초기화 실패 4종(펄스·설정·STOP 입력·WDT·NVS), 통신두절 정지·알람 해제, 프로파일 갱신 실패, 1축 빌드 | 204 checks, **0 failures** |
| `test_cfg_syntax.py` (pytest) | 전체 C 파일을 IDF 스텁에 대해 `gcc -fsyntax-only`, sdkconfig 조합 9종 + 모델 조각 4종(bool n은 정의하지 않는 IDF 규칙), `fw_cfg.h` 매크로 정적 단언, 음성 대조(미정의 bool을 값으로 쓰면 실패), bool은 `#if` 줄에만, 쓰인 CONFIG 전부 Kconfig 선언, 기본 핀이 기준선 신호표와 일치·중복 없음, `sdkconfig.defaults` 형식과 안전 옵션, `app_main` 첫 호출이 `fw_hal_oe_early_forbid` | 31 passed |

개발 중 시험이 잡은 것(수정 후 재실행, 시험을 지우지 않음): ① `rtu_baud` 115200이 U16에 안 들어감 → ÷100 인코딩(R12). ② 감속 정지 중 상태가 알람보다 우선해야 하는데 반대였음(`fw_axis.c` 수정). 시험 쪽 수정: 업타임 상위 워드와 부분 쓰기 기대값 오류, 감속 정지 종료 주파수 검사는 시험의 펄스 간격(200)이 거칠어 20펄스 간격 + 12 kHz 이하로 바꿨다(실제 2 ms 갱신은 5 kHz에서 약 10펄스).

**이 시험이 증명하지 않는 것:** IDF API 서명, 인터럽트·IRAM 동작, 타이밍, MCPWM/PCNT 실동작, NVS 실제 기록, 네트워크·UART, 실장비 전부(§8).
