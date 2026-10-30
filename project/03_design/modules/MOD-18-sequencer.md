# 설계→코딩 인터페이스: sequencer (단독 운전 순서 실행, 권장)

| 모듈 ID | 버전 | 작성 | 대상 언어/플랫폼 | 추적 SRS ID |
|---|---|---|---|---|
| MOD-18 | 0.1 | architect | C(MCU 무관) | FR-013(권장) |

## 1. 책임 (이 모듈이 하는 일 / 하지 않는 일)
- 한다: 저장된 운전 순서(최대 N 스텝, N = 미확인(Q22) → 작업값 32 [가정])를 상위 장치 없이 실행. 스텝 종류는 FR-013 한정: MOVE_ABS, MOVE_REL(축·위치·속도), SET_SPEED(다음 이동 기본 속도), WAIT_TIME(ms), WAIT_INPUT(축·신호·레벨·시간 초과). 조건 분기·연산 없음. 시작·정지·반복 횟수, 현재 스텝 보고. 저장은 NVM_SEQ.
- 하지 않는다: 축 직접 제어(MOD-09 명령으로만 구동), 보간(FR-014 범위 밖).

## 2. 의존성
- 사용: MOD-09(`ax_post_command`, 스냅샷), MOD-11(입력 대기), MOD-06(NVM_SEQ), MOD-13(알람 시 중단 기록).
- 사용하는 쪽: MOD-14(0x6000 영역), MOD-21(순서 편집), 기동 입력(시작 신호 입력 할당은 미확인 — 범용 입력이 SRS에 없음, architecture 이슈 #6).

## 3. 입출력 정의
| 구분 | 이름 | 타입/단위 | 범위 | 설명 |
|---|---|---|---|---|
| 입력 | step[i] | {type U16, axis U16, p1 S32, p2 U32, p3 U32} | i < N | 레지스터 0x6000~ (8레지스터/스텝 [가정]) |
| 입력 | seq_command | U16 | RUN, STOP, RESET | |
| 입력 | repeat | U16 | 0 = 무한 | |
| 출력 | seq_state, cur_step, loop_count | U16 | | |
| 오류 | seq_error | U16 | 축 거부 사유 코드, 입력 대기 시간 초과 | |

공개 API(시그니처 수준):
```c
void     seq_init(void);
uint16_t seq_run(uint16_t repeat);
void     seq_stop(void);
void     seq_task_tick(void);          /* MOTION task context or own low-priority task */
seq_status_t seq_status(void);
```

## 4. 제약 조건
- 비상정지·알람 시 즉시 중단, 자동 재개 없음.
- 순서 실행 중 외부 Modbus 축 명령 처리 규칙: 거부 [가정].
- 단독 운전 시작 수단(전원 투입 자동 시작 / Modbus 명령 / 입력 신호)은 미확인 → v1은 Modbus·PC 도구 명령으로 시작 [가정].

## 5. 완료 기준 (각 항목은 단위 테스트로 검증 가능해야 함)
| # | 기준 | 검증 방법 |
|---|---|---|
| 1 | 스텝 종류별 동작 순서 일치 | 호스트 시뮬(가짜 MOD-09) |
| 2 | 상위 장치 분리 상태에서 N 스텝 반복 실행 | 시연(HIL, FR-013) |
| 3 | 실행 중 비상정지 → 중단, 해제 후 자동 재개 없음 | HIL |

## 6. 미확인·가정
- N(Q22), 시작 수단, 스텝 레지스터 형식 [가정]. 권장 요구이므로 구현 순서 마지막.
