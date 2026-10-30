# 설계→코딩 인터페이스: estop_safety (비상정지 소프트웨어 경로)

| 모듈 ID | 버전 | 작성 | 대상 언어/플랫폼 | 추적 SRS ID |
|---|---|---|---|---|
| MOD-10 | 0.1 | architect | C(MCU 무관) | FR-010, NFR-011, NFR-020 |

## 1. 책임 (이 모듈이 하는 일 / 하지 않는 일)
- 한다: 비상정지 2차 경로(소프트웨어). 비상정지 입력 활성 엣지(ISR)에서 전 축 즉시 정지(`pe_request_stop(QUICK)`), 래치 설정, 정책(`estop_pos_policy`)에 따라 전 축 위치 무효 표시. 래치 중 모든 기동 명령 거부 근거 제공. 해제 조건(입력 정상 + 명시적 해제 명령) 판정. 하드웨어 게이트 되읽기(DI_GATE_FB)와 입력 상태 불일치 진단. 기동 시 입력이 비상정지 상태이면 래치 상태로 시작.
- 하지 않는다: 하드웨어 1차 경로(회로: 포토커플러 → AM26C31 enable, interface-spec §6). 이 모듈이 고장 나도 1차 경로는 동작해야 한다. 안전 기능 인증·이중화(STD-004 범위 밖).

## 2. 의존성
- 사용: MOD-03(ESTOP 엣지 인터럽트·되읽기), MOD-07(즉시 정지), MOD-13(E-ESTOP 알람 기록), MOD-12(`estop_pos_policy`).
- 사용하는 쪽: MOD-09(래치 조회), MOD-14(ESTOP_RELEASE 명령·상태 레지스터), MOD-21(PC 도구 해제).

## 3. 입출력 정의
| 구분 | 이름 | 타입/단위 | 범위 | 설명 |
|---|---|---|---|---|
| 입력 | ESTOP 원시 엣지 | (level, t_us) | | 활성 = 입력 전류 없음(NC 전제 [가정]) |
| 입력 | release 요청 | 출처(conn_id/도구) | | sys_command = 1 |
| 출력 | estop_state | enum | 0 정상, 1 비상정지 중, 2 래치 유지(해제 대기) | interface-spec §5.3 |
| 출력 | latched | bool | | MOD-09 조회 |
| 출력 | 진단 | gate_mismatch | bool | 입력 활성인데 게이트 되읽기가 "출력 허용" → 회로 이상 경고 |
| 오류 | release 결과 | U16 | 0 성공, 0x10 거부(입력 활성) | sys_cmd_result |

공개 API(시그니처 수준):
```c
void     es_init(void);                 /* reads input at boot; latched if active */
bool     es_latched(void);
uint16_t es_state(void);
uint16_t es_request_release(int8_t origin);   /* returns result code */
bool     es_gate_mismatch(void);
uint32_t es_last_trip_us(void);         /* timestamp of last trip edge */
```

동작(의사코드):
```
ISR on ESTOP active edge (highest priority among EXTI):
    for each axis: pe_request_stop(axis, QUICK)      /* hardware gate already cut outputs */
    latched = true; state = 1; trip_t = t_us
    if estop_pos_policy == 0: mark all axes position-invalid
    post alarm E-ESTOP (via lock-free ring to L2)
L2 tick:
    if input inactive and latched: state = 2
    gate_mismatch = input_active && gate_fb_says_enabled
es_request_release():
    if input active: return 0x10
    latched = false; state = 0; return 0   /* servo/start need separate commands */
```

## 4. 제약 조건
- 타이밍: 입력 엣지 → `pe_request_stop` 호출까지 ≤ 미확인 µs(측정값 공개, NFR-011). 1차 하드웨어 경로가 더 빠르며, NFR-011 공개값은 하드웨어 경로 실측(T-05)으로 정한다.
- 해제 후 자동 재기동 금지(FR-010). 해제 명령은 래치만 풀고 축 상태는 READY/SERVO_OFF로 유지.
- 디바운스 없음(첫 엣지에 즉시 반응). 해제 쪽 판정만 MOD-11 필터 적용 [가정].
- 고지: 이 모듈 기능을 "안전 기능"으로 표기하지 않는다(NFR-020). HMI·PC 도구 문구는 MOD-21·23.

## 5. 완료 기준 (각 항목은 단위 테스트로 검증 가능해야 함)
| # | 기준 | 검증 방법 |
|---|---|---|
| 1 | 운전 중 비상정지 입력 → 전 축 펄스 정지, 마지막 펄스까지 시간 측정 | HIL T-05 |
| 2 | 래치 중 START·JOG·HOME 명령 거부(0x10) | 호스트 시뮬 + HIL |
| 3 | 입력 활성 중 해제 요청 거부, 입력 정상 후 해제 성공, 해제 후 자동 기동 없음 | HIL |
| 4 | 전원 투입 시 입력 활성이면 래치 상태로 시작 | HIL |
| 5 | MCU 정지(디버거 정지) 상태에서도 비상정지 입력으로 출력 차단(1차 경로 독립성) | HIL |
| 6 | 정책 0에서 비상정지 후 position-invalid bit, START_ABS 거부(require_home=1) | HIL |

## 6. 미확인·가정
- NC 접점 전제 [가정](interface-spec 이슈 #3).
- 위치 처리 정책 기본값 0(위치 무효) [가정, DC-08].
- 응답 시간 목표(NFR-011) 미확인(Q14).
