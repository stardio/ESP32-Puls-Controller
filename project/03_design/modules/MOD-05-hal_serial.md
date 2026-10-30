# 설계→코딩 인터페이스: hal_serial (RS-485 시리얼 HAL, 조건부)

| 모듈 ID | 버전 | 작성 | 대상 언어/플랫폼 | 추적 SRS ID |
|---|---|---|---|---|
| MOD-05 | 0.1 | architect | C — port_stm32f4(USART + DMA [가정]) / port_esp32(UART RS-485 모드 [가정]) | FR-005(RTU [가정]), NFR-010 |

> 조건부 모듈: DC-06에서 RS-485 실장이 결정될 때만 구현한다. API는 지금 고정해 둔다.

## 1. 책임 (이 모듈이 하는 일 / 하지 않는 일)
- 한다: UART 설정(보레이트·패리티·정지 비트), RS-485 송신 enable(DE) 자동 제어, 수신 바이트 스트림과 "문자 간 무신호 시간" 감지(Modbus RTU 프레임 경계 t3.5 판정용 타임스탬프 제공).
- 하지 않는다: RTU 프레임 해석·CRC(MOD-16).

## 2. 의존성
- 사용: MOD-01. 사용하는 쪽: MOD-16.

## 3. 입출력 정의
| 구분 | 이름 | 타입/단위 | 범위 | 설명 |
|---|---|---|---|---|
| 입력 | baud | uint32_t, bps | 9,600~115,200 [가정] | SN65HVD72 상한 미확인 |
| 입력 | parity / stop | enum | NONE/EVEN/ODD, 1/2 | |
| 입력 | idle_us | uint32_t, µs | t3.5 상당 | 프레임 끝 판정 무신호 시간 |
| 출력 | rx 프레임 콜백 | (buf, len) | ≤ 256 B | 무신호 감지 시 전달 |
| 오류 | hal_err_t | | HAL_EIO(프레이밍·패리티·오버런) | 오류 카운터 증가 |

공개 API(시그니처 수준):
```c
typedef void (*hal_serial_rx_cb_t)(const uint8_t *buf, size_t len, uint32_t err_flags, void *ctx);
hal_err_t hal_serial_open(uint32_t baud, hal_parity_t parity, uint8_t stop_bits, uint32_t idle_us,
                          hal_serial_rx_cb_t cb, void *ctx);
hal_err_t hal_serial_write(const uint8_t *buf, size_t len);   /* DE asserted during TX, released after last stop bit */
void      hal_serial_close(void);
```

## 4. 제약 조건
- DE 해제는 마지막 정지 비트 송신 완료 후(송신 완료 플래그 기준). 소프트웨어 지연에 의존하지 않는다.
- 인터럽트 우선순위는 L1보다 낮다. 수신 처리 부하가 펄스 엔진에 영향이 없어야 한다(T-03 확장 시 RTU 포함 [가정]).
- 콜백은 COMM 태스크 문맥으로 전달(ISR → 큐) [가정].

## 5. 완료 기준 (각 항목은 단위 테스트로 검증 가능해야 함)
| # | 기준 | 검증 방법 |
|---|---|---|
| 1 | 설정한 보레이트·패리티로 루프백 송수신 일치 | HIL(USB-RS485 변환기 [가정]) |
| 2 | 무신호 시간 기준 프레임 분리 정확(연속 2프레임) | HIL |
| 3 | DE 파형: 송신 구간만 활성 | HIL(오실로스코프) |

## 6. 미확인·가정
- DC-06 결과에 따라 구현 여부 결정. 지원 보레이트 범위 [가정].
- 반이중 2선식 [가정].
