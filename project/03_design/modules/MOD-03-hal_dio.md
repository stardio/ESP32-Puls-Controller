# 설계→코딩 인터페이스: hal_dio (디지털 입출력 HAL)

| 모듈 ID | 버전 | 작성 | 대상 언어/플랫폼 | 추적 SRS ID |
|---|---|---|---|---|
| MOD-03 | 0.1 | architect | C — port_stm32f4(GPIO + EXTI) / port_esp32(GPIO ISR) | FR-003, FR-008, FR-009, FR-010, FR-011, NFR-008 |

## 1. 책임 (이 모듈이 하는 일 / 하지 않는 일)
- 한다: 논리 신호 ID(DI_AXn_HOME 등)를 물리 핀에 대응시키고, 입력 레벨 읽기, 엣지 인터럽트 등록, 출력(서보 ON, LED) 쓰기, 모델 식별 스트랩 읽기, 비상정지 게이트 되읽기를 제공한다.
- 하지 않는다: 접점 논리(NO/NC) 적용·디지털 필터(MOD-11), 정지 판단(MOD-09·10). 하드웨어 비상정지 게이트 제어(회로가 직접 함, MCU는 되읽기만).

## 2. 의존성
- 사용: MOD-01. 핀 배정표(interface-spec §2.3, 포트별 `dio_pinmap.c`).
- 사용하는 쪽: MOD-10, MOD-11, MOD-19.

## 3. 입출력 정의
| 구분 | 이름 | 타입/단위 | 범위 | 설명 |
|---|---|---|---|---|
| 입력 | hal_di_t | enum | DI_AX1_HOME, DI_AX1_LIMP, DI_AX1_LIMN, DI_AX1_ALM, DI_AX1_INP, DI_AX2_*(동일 5종), DI_ESTOP, DI_GATE_FB | 논리 ID |
| 입력 | hal_do_t | enum | DO_AX1_SVON, DO_AX2_SVON, DO_LED_RUN, DO_LED_ERR | |
| 입력 | edge | enum | RISING / FALLING / BOTH | 원시 레벨 기준 |
| 출력 | 원시 레벨 | bool | | 포토커플러 출력 측 레벨(접점 논리 미적용) |
| 출력 | 엣지 콜백 | (id, level, t_us) | | ISR 문맥, 엣지 시각 µs |
| 출력 | model strap | uint8_t | 0~3 | 모델 식별 원시값(MOD-19가 해석) |
| 오류 | hal_err_t | | HAL_EINVAL(미실장 신호: 1축 모델의 AX2) | |

공개 API(시그니처 수준):
```c
typedef void (*hal_di_cb_t)(hal_di_t id, bool level, uint32_t t_us, void *ctx);  /* ISR context */
hal_err_t hal_dio_init(void);
bool      hal_di_read(hal_di_t id);
uint32_t  hal_di_read_all(void);                       /* bit i = hal_di_t i */
hal_err_t hal_di_irq_enable(hal_di_t id, hal_edge_t edge, hal_di_cb_t cb, void *ctx);
hal_err_t hal_di_irq_disable(hal_di_t id);
hal_err_t hal_do_write(hal_do_t id, bool on);
bool      hal_do_state(hal_do_t id);
uint8_t   hal_dio_model_strap(void);
```

## 4. 제약 조건
- 타이밍: 엣지 → 콜백 진입 지연은 포트별 측정값을 기록(T-08과 합산해 리밋·원점 정지 오차 산정). 비상정지 엣지 인터럽트는 리밋·알람보다 높은 우선순위, 펄스 엔진 구간 인터럽트보다 낮거나 같은 우선순위 [가정](architecture §3 L1).
- 입력 회로는 포토커플러 반전 여부가 회로에 따라 다르므로 "원시 레벨"만 다루고, 활성 판정은 MOD-11이 `input_logic` 파라미터와 회로 반전표(포트 상수)를 함께 적용한다.
- ISR 안전: `hal_di_read`, `hal_di_read_all`, `hal_do_write`는 ISR 호출 가능.
- 1축 모델에서 AX2 신호 접근은 HAL_EINVAL(모델 정보는 MOD-19가 초기화 시 전달).

## 5. 완료 기준 (각 항목은 단위 테스트로 검증 가능해야 함)
| # | 기준 | 검증 방법 |
|---|---|---|
| 1 | 모든 DI 원시 레벨이 입력 단자 상태와 일치 | HIL(I/O 모의 장치, MOD-22) |
| 2 | 엣지 콜백이 지정 엣지에서만 1회 호출, t_us 기록 | HIL |
| 3 | 엣지 → 콜백 지연 측정값 기록(최대값) | HIL(T-08 보조) |
| 4 | 서보 ON 출력 쓰기/되읽기 일치, 단자 레벨 정상 | HIL |
| 5 | 모델 스트랩 0/1 조합 판독 | HIL(스트랩 저항 교체) |

## 6. 미확인·가정
- 물리 핀 번호 전부 미확인(interface-spec §2, 데이터시트 AF 대조 전).
- 원점 정밀 래치에 하드웨어 입력 캡처를 쓸지는 핀 여유에 따름 [가정: 기본은 EXTI + 계수 스냅샷].
- 게이트 되읽기(DI_GATE_FB) 실장 [가정].
