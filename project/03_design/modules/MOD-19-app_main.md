# 설계→코딩 인터페이스: app_main (기동·태스크 구성·모델 식별·감시)

| 모듈 ID | 버전 | 작성 | 대상 언어/플랫폼 | 추적 SRS ID |
|---|---|---|---|---|
| MOD-19 | 0.1 | architect | C(MCU 무관, 포트별 진입점만 다름) | FR-003, NFR-002, NFR-003, NFR-013, NFR-014, CON-011 |

## 1. 책임 (이 모듈이 하는 일 / 하지 않는 일)
- 한다: 기동 순서 실행, 모델 식별(스트랩 → model_id 1/2, 단일 바이너리, DC-07), 축 수에 따른 모듈 초기화, 태스크·인터럽트 우선순위 배정, 태스크 생존 감시(각 태스크 check-in → 하드웨어 워치독 갱신), 자가 점검 후 펌웨어 확정(MOD-17), 상태 LED, 리셋 원인 기록.
- 하지 않는다: 기능 로직(각 모듈).

## 2. 의존성
- 사용: 전 모듈(MOD-01~18).
- 사용하는 쪽: 없음(최상위).

## 3. 입출력 정의
| 구분 | 이름 | 타입/단위 | 범위 | 설명 |
|---|---|---|---|---|
| 입력 | model strap | uint8_t | | MOD-03 |
| 출력 | model_id, axis_count | U16 | 1/2 | 레지스터 0x0000·0x0001 |
| 출력 | 태스크 구성 | | 아래 표 | |
| 오류 | 스트랩 미정의 값 | | 기동 금지 + ERR LED [가정] | |

공개 API(시그니처 수준):
```c
void    app_main_entry(void);         /* called from port startup */
uint8_t app_model_id(void);
uint8_t app_axis_count(void);
void    app_checkin(app_task_id_t id); /* each task calls once per period */
```

실행 구성(초안):
| 문맥 | 내용 | 주기·우선순위 | 비고 |
|---|---|---|---|
| ISR L1-a | hal_pulse 구간 완료·언더런(MOD-07) | 최우선 | RTOS API 금지, HAL_FAST_CODE |
| ISR L1-b | ESTOP 엣지(MOD-10) | L1-a 다음 | |
| ISR L1-c | 리밋·알람·원점 엣지(MOD-11) | L1-b 다음 | |
| ISR 일반 | 이더넷 DMA, UART | L1보다 낮음 | RTOS 호출 가능 등급 |
| Task MOTION | MOD-09·11 tick, MOD-07 이벤트 소비, MOD-18 | 1 ms [가정], 최고 태스크 우선순위 | |
| Task COMM-TCP | MOD-15 | 이벤트 구동 | |
| Task COMM-RTU | MOD-16(조건부) | 이벤트 구동 | |
| Task SERVICE | MOD-12 저장, MOD-13 플러시, MOD-17 기록, LED | 10 ms [가정], 최저 | 플래시 작업 전담 |

기동 순서(의사코드):
```
hal_sys_init -> hal_dio_init -> model = strap -> es_init (latch if estop active)
ps_init_and_load -> ad_init (record reset cause) -> pe_init/io_init/ax_init(axis_count)
hal_net_init(params) -> mb_tcp_start -> [mb_rtu_start] -> create tasks -> start scheduler
after T_selftest (5 s [assumed]) with all tasks checked in: fwu_confirm_running_image
watchdog: kick only if every task checked in within its window
```

## 4. 제약 조건
- 인터럽트 우선순위 표는 포트별 상수로 두되 상대 순서(위 표)는 공통.
- 워치독 시간: 미확인(MCU 한도) → 작업값 500 ms [가정].
- 전원 투입 후 운전 가능 시간(NFR-014, 선택)은 측정값 기록.
- 1축 모델: 축2 관련 태스크·ISR·레지스터를 활성화하지 않는다.

## 5. 완료 기준 (각 항목은 단위 테스트로 검증 가능해야 함)
| # | 기준 | 검증 방법 |
|---|---|---|
| 1 | 스트랩별 model_id·axis_count 정상, 1축에서 축2 접근 예외 | HIL |
| 2 | 임의 태스크 정지(시험 훅) → 워치독 리셋, E-WDT 기록 | HIL(NFR-013) |
| 3 | 전원 투입 → Modbus 응답 가능까지 시간 기록 | HIL(NFR-014) |
| 4 | 연속 운전 시험(시간 미확인, Q15-잔여)에서 리셋 0 | HIL(NFR-013, ⑤) |

## 6. 미확인·가정
- 태스크 주기·워치독·자가 점검 시간 [가정]. 모델 스트랩 방식 [가정, DC-07].
