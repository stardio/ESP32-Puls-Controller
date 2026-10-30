# 설계→코딩 인터페이스: axis_control (축 상태기·명령 처리)

| 모듈 ID | 버전 | 작성 | 대상 언어/플랫폼 | 추적 SRS ID |
|---|---|---|---|---|
| MOD-09 | 0.1 | architect | C(MCU 무관) | FR-003, FR-004, FR-006, FR-008, FR-009, FR-010, FR-011, FR-012, FR-019, FR-020, FR-023(b) |

## 1. 책임 (이 모듈이 하는 일 / 하지 않는 일)
- 한다: 축별 상태기(interface-spec §5.4 `axis_state`) 운영. 명령(START_ABS/REL, STOP, QUICK_STOP, HOME, ALARM_RESET, SERVO_ON/OFF, JOG±, JOG_STOP, SET_POSITION) 수락·거부 판정과 결과 코드. 소프트 리밋 사전 검사(FR-020), 위치 무효 시 절대 이동 거부(`require_home`). 원점복귀 절차(FR-008), 완료 판정(펄스 종료 또는 INP 대기, FR-006·FR-011(b)), 리밋·드라이브 알람·통신두절·JOG 유지 상실에 대한 정지 반응, 서보 ON 출력(FR-011(c)), 마지막 정지 원인 기록, 축 상태 스냅샷 발행.
- 하지 않는다: 펄스 생성(MOD-07), 입력 필터(MOD-11), 비상정지 래치·해제(MOD-10, 이 모듈은 래치 상태를 조회만), Modbus 해석(MOD-14).

## 2. 의존성
- 사용: MOD-07, MOD-08(정지 거리 계산), MOD-11(입력 상태·이벤트), MOD-10(래치 조회), MOD-12(축 파라미터 읽기), MOD-13(알람 발생·정지 원인 기록), MOD-01(OSAL).
- 사용하는 쪽: MOD-14(명령 큐 투입·스냅샷 읽기), MOD-15/16(연결 생존 통지), MOD-18(순서 실행), MOD-19(태스크 구동).

## 3. 입출력 정의
| 구분 | 이름 | 타입/단위 | 범위 | 설명 |
|---|---|---|---|---|
| 입력 | ax_cmd_t | 구조체 | cmd 코드(interface-spec §5.5), target, speed, accel_ms, decel_ms, origin(conn_id) | 명령 큐(COMM → MOTION) |
| 입력 | 입력 이벤트 | io_evt_t(MOD-11) | HOME/LIMP/LIMN/ALM/INP 변화 | |
| 입력 | pe_evt_t(MOD-07) | | DONE, STOPPED, UNDERRUN, HW_FAULT | |
| 입력 | 연결 생존 | conn_id, alive/lost | | JOG 유지·통신두절(FR-012) |
| 출력 | ax_snapshot_t | 구조체 | state, status bits, position, speed, alarm, stop_cause, cmd_result, inputs | 1 ms마다 seqlock으로 발행 [가정] |
| 출력 | 서보 ON | bool | | MOD-11 경유 DO |
| 오류 | cmd_result | U16 코드 | 0x00~0x17(interface-spec §5.5) | 거부 사유 |

공개 API(시그니처 수준):
```c
typedef struct { uint16_t cmd; int32_t target; uint32_t speed; uint16_t accel_ms, decel_ms; uint32_t jog_speed; int8_t conn_id; } ax_cmd_t;
typedef struct { uint32_t seq; uint16_t state, status, alarm, stop_cause, cmd_result, inputs;
                 int32_t position, speed, target; uint32_t move_count; } ax_snapshot_t;

void     ax_init(uint8_t axis_count);
hal_err_t ax_post_command(uint8_t axis, const ax_cmd_t *cmd);   /* COMM tasks -> queue */
void     ax_task_tick(void);                                     /* MOTION task, 1 ms [assumed] */
bool     ax_get_snapshot(uint8_t axis, ax_snapshot_t *out);      /* lock-free read (seqlock retry) */
void     ax_notify_conn(int8_t conn_id, bool alive);             /* from MOD-15/16 */
void     ax_stop_all(uint16_t cause);                            /* STOP_ALL, comm loss */
```

상태기(의사코드):
```
SERVO_OFF --SERVO_ON--> READY
READY --START_ABS/REL [checks ok]--> MOVING --pe DONE [inp_wait?INP:–]--> READY(+완료 bit)
READY --JOG_POS/NEG--> JOG --JOG_STOP / coil off / keepalive lost--> DECEL_STOP --> READY
READY --HOME--> HOMING(step machine) --success--> READY(+homed, position := home_offset) | fail --> ALARM
any moving --STOP--> DECEL_STOP ; --QUICK_STOP--> READY(stopped)
any --drive ALM input--> ALARM (stop per alarm_stop_mode) --ALARM_RESET [ALM inactive]--> READY
any --limit in moving dir--> stop per limit_stop_mode, stop_cause=LIMP/LIMN, state READY (move away allowed)
any --estop latched (MOD-10)--> ESTOP ; --release--> SERVO state unchanged, READY only after release
checks for START: !estop, !alarm, servo on, state READY, target within soft limits (if enabled), !(position invalid && require_home), limit input not active in move direction
```

원점복귀 절차(home_method 0, [가정]):
```
1 move toward home_dir at home_speed_high until HOME edge (or limit -> method 1 reverse)
2 decel stop, reverse at home_speed_creep until HOME edge released, then re-approach at creep
3 on HOME edge at creep: latch hardware count (MOD-11 ISR snapshot), stop
4 position := home_offset + (current_count - latched_count) ; set homed; clear position-invalid
timeout home_timeout_s -> E-HOMEFAIL
```

## 4. 제약 조건
- 실행 문맥: MOTION 등급 태스크, 주기 1 ms [가정]. 리밋·알람 즉시 정지는 MOD-11 ISR이 MOD-07을 직접 호출하고, 이 모듈은 다음 틱에 상태를 맞춘다(정지 지연이 태스크 주기에 묶이지 않게).
- 축 독립(FR-003): 축별 상태·큐 분리. 한 축 알람이 다른 축을 멈추지 않는다(비상정지·STOP_ALL·전체 통신두절 제외).
- 명령 처리 결정성: 명령은 도착 순서대로, 축별 큐 깊이 8 [가정]. 넘치면 cmd_result 거부(0x13).
- JOG 안전: JOG를 시작한 conn_id가 끊기거나 `jog_keepalive_ms` 동안 요청이 없으면 감속 정지(interface-spec §5.5, [가정]).
- 스냅샷 발행은 L3 읽기와 경합 없이(seqlock) 32비트 값 찢김 없이 제공.

## 5. 완료 기준 (각 항목은 단위 테스트로 검증 가능해야 함)
| # | 기준 | 검증 방법 |
|---|---|---|
| 1 | 상태 전이 표의 모든 전이·거부 조건을 명령별로 검증(cmd_result 코드 일치) | 호스트 시뮬(가짜 MOD-07·11) |
| 2 | 소프트 리밋 초과 목표 거부, JOG는 리밋 위치에서 정지 | 호스트 시뮬 + HIL |
| 3 | 원점복귀: 모의 HOME 엣지로 원점 위치 반복 정밀도 기록(외부 카운터 기준) | HIL(MOD-22 I/O 모의) |
| 4 | 리밋 입력 시 해당 방향 정지, 반대 방향 이동 허용 | HIL(FR-009) |
| 5 | 드라이브 알람 입력 → 해당 축만 정지, 알람 코드, 해제 후 재기동 | HIL(FR-011(a)) |
| 6 | INP 대기 모드에서 완료 bit 전환 시점 = INP 입력 후 | HIL(FR-006, FR-011(b)) |
| 7 | JOG 중 TCP 연결 끊김 → 감속 정지 | HIL |
| 8 | 2축 모델: 축1 운전 중 축2 기동·정지·알람이 축1에 영향 없음 | HIL(FR-003) |

## 6. 미확인·가정
- 원점복귀 방식 종류(home_method 0~2)는 [가정]. 대표 드라이브·고객 설비 관행 확인 필요.
- 리밋 정지 목표 시간 미확인(Q14). 즉시 정지 기본 [가정].
- 리밋 정지를 알람이 아닌 "정지 원인"으로만 둘지 [가정] — G3 확인.
- 태스크 주기 1 ms, 큐 깊이 8 [가정].
