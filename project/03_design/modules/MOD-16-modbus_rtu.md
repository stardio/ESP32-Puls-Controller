# 설계→코딩 인터페이스: modbus_rtu (Modbus RTU 슬레이브, 조건부)

| 모듈 ID | 버전 | 작성 | 대상 언어/플랫폼 | 추적 SRS ID |
|---|---|---|---|---|
| MOD-16 | 0.1 | architect | C(MCU 무관, MOD-05 위) | FR-005(RTU 필수 [가정]), FR-012, NFR-010, STD-003 |

> 조건부: DC-06(RS-485 실장)에서 실장으로 결정될 때 구현한다.

## 1. 책임 (이 모듈이 하는 일 / 하지 않는 일)
- 한다: RTU 프레임(주소 + PDU + CRC16) 수신·검증, 자기 주소 요청만 처리, 브로드캐스트(주소 0)는 쓰기 처리 후 무응답, 응답 송신. 프레임 경계는 MOD-05 무신호 감지(t3.5) 사용. RTU 수신 두절 감시(FR-012 RTU 측). 통신 오류 계수.
- 하지 않는다: PDU 해석(MOD-14 공용 → TCP와 같은 맵, interface-spec §5).

## 2. 의존성
- 사용: MOD-05, MOD-14, MOD-12(rtu_baud/parity/addr), MOD-09(통지).
- 사용하는 쪽: MOD-19.

## 3. 입출력 정의
| 구분 | 이름 | 타입/단위 | 범위 | 설명 |
|---|---|---|---|---|
| 입력 | RTU 프레임 | byte | ≤ 256 B | |
| 입력 | rtu_addr | U16 | 1~247 [규격 원문 대조 전] | |
| 출력 | 응답 프레임 | byte | | |
| 출력 | 오류 계수 | CRC 오류, 프레이밍 오류 | | 진단 |
| 오류 | CRC 불일치 | | 무응답(폐기) | |

공개 API(시그니처 수준):
```c
hal_err_t mb_rtu_start(uint8_t addr, uint32_t baud, hal_parity_t parity);
void      mb_rtu_service_tick(void);    /* comm-loss evaluation */
uint16_t  mb_crc16(const uint8_t *buf, size_t len);   /* pure function, host-testable */
```

## 4. 제약 조건
- conn_id는 RTU 전용 고정값(예: −2) [가정]으로 MOD-09에 전달(JOG 유지 판정 공통 규칙 적용).
- 응답 시작은 요청 프레임 끝(t3.5) 이후. 응답 시간 측정값 공개(NFR-010).
- 태스크 등급 COMM.

## 5. 완료 기준 (각 항목은 단위 테스트로 검증 가능해야 함)
| # | 기준 | 검증 방법 |
|---|---|---|
| 1 | CRC16 규격 예제 값 일치 | 호스트 단위 테스트 |
| 2 | 제3자 RTU 마스터로 FR-004 전 항목 | HIL |
| 3 | 다른 주소·CRC 오류 프레임 무응답 | HIL |
| 4 | 브로드캐스트 쓰기 반영·무응답 | HIL |
| 5 | RTU 배선 차단 → 설정 시간 후 감속 정지(사용 시) | HIL(FR-012) |

## 6. 미확인·가정
- 구현 여부 DC-06·Q23. 보레이트 범위 [가정](MOD-05).
