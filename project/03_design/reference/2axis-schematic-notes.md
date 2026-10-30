# 2축 캐리어 보드 회로도 판독 메모 (메인 세션, 2026-10-30)

원본: `reference/2axis-schematic.pdf` (사용자 제공, 1쪽). 아래는 회로도 이미지 판독 결과이며, 판독이 불확실한 값은 (판독 불확실)로 표시했다. 설계 근거로 쓸 때는 원본을 다시 확인한다.

## 구성 블록
| 블록 | 부품 | 연결 |
|---|---|---|
| MCU 보드 장착 | J1 ESP32-P4A, J2 ESP32-P4B (각 20핀 헤더) | ESP32-P4 보드를 캐리어에 꽂는 구조. J2에 VBUS(ESP_5V), +3V3, EN, RUN |
| 이더넷 | J3 W5500 모듈 (SPI) | GPIO2 MISO, GPIO3 MOSI, GPIO4 SCLK, GPIO5 CS, GPIO6 INT, GPIO14 RESET. R1·R2 10k 풀업 |
| 펄스 출력 절연 | U1, U16 ISO7720FDR (디지털 아이솔레이터, 2채널) | U1: GPIO25(INA), GPIO32(INB) / U16: GPIO20, GPIO21. 입력 직렬 33Ω |
| 차동 라인 드라이버 | U7 AM26C31IDR (4채널, 5V_ISO 전원) | 1A·2A ← U1, 3A·4A ← U16. 출력 1Y/1Z·2Y/2Z → J4 PULS1, 3Y/3Z·4Y/4Z → J14 PULS2 (직렬 0Ω) |
| 펄스 출력 인에이블 | U2 TLP2361 (고속 포토커플러) | GPIO33 → R6 560Ω → U2 → AM26C31 G 단자 (R11 10k 풀업, 5V_ISO) |
| 디지털 입력 4점 | U9~U12 TLP293, 24V 필드측 R 2.2k, 10k | DI1 GPIO54, DI2 GPIO19, DI3 GPIO18, DI4 GPIO17. 커넥터 J15 D/I (6핀, +24V_ISO1500 공급) |
| 디지털 출력 4점 | U3~U6 TLP293 + Q1~Q4 MMBT2222A + D1~D4 SS210 | DO1 GPIO23, DO2 GPIO26, DO3 GPIO27, DO4 GPIO46. 커넥터 J6 D/O (NPN 싱크, +24V_ISO1500) |
| RS-485 | U17 ISO1500 (절연 트랜시버), L2 WE744232090 공통모드 초크, R68 120Ω + JP6 종단 점퍼, D17 SM712 TVS | GPIO24 TX(D), GPIO47 RX(R), GPIO48 RE/DE. 커넥터 J12 (A, B, GND_ISO5, SHIELD) |
| 전원 입력 | J5 24V, F1 2A 퓨즈, D5 SMBJ30A TVS, C8 22µF/50V | +24V_ISO1500 |
| 절연 DC-DC 1 | U15 B2405S-2WR3 | 24V → ESP_5V (MCU 보드 전원) |
| 절연 DC-DC 2 | U19 TMV2405S | 24V → 5V_ISO (펄스·RS-485 절연측) |

## 회로도 표의 GPIO 배정
| GPIO | 용도 | 방향 | 비고 |
|---|---|---|---|
| 2, 3, 4, 5, 6 | W5500 MISO, MOSI, SCLK, CS, INT | SPI | |
| 7, 8 | 예비 (I2C) | I/O | |
| 14 | W5500 RESET | OUT | |
| 15, 16 | (표에 번호만 있음) | | 판독 불확실 |
| 17, 18, 19, 54 | DI4, DI3, DI2, DI1 | IN | 표에서 GPIO54=DI1 |
| 20, 21 | CH2 A Pulse, CH2 B Pulse | OUT | Servo B (2축) |
| 22 | (표에 번호만 있음) | | 판독 불확실 |
| 23, 26, 27, 46 | DO1, DO2, DO3, DO4 | OUT | |
| 24, 47, 48 | RS-485 TX, RX, RE/DE | | ISO1500 |
| 25, 32 | CH1 A Pulse, CH1 B Pulse | OUT | Servo A (1축) |
| 33 | Pulse Output Enable (AM26C31 Enable) | OUT | TLP2361 경유 |

## 기존 설계·결정과 다른 점 (메인 세션 1차 대조, architect 확인 필요)
1. **이더넷이 W5500 모듈이다.** D-027은 "HMI는 온보드 IP101(192.168.0.220)"로 기록했다. servo1axis MODBUS_TCP_REGISTER_MAP.md는 "EasyView 연결 대상은 W5500 192.168.0.230"이라고 적었다. 2축 보드에서 HMI가 어느 포트를 쓰는지 사용자 확인이 필요하다.
2. **비상정지·STOP 입력:** GPIO19는 DI2(24V 포토커플러 입력)이고, GPIO18은 DI3이다. 둘 다 입력으로 쓰인다(D-027 GPIO19와 일치, 기존 코드 GPIO18 표기와 공존).
3. **축2 핀 확정:** 축2 펄스는 GPIO20, 방향(B)은 GPIO21이다. 시험 펌웨어의 축2 임시값 2/3/4는 W5500 SPI 핀과 겹쳤으며, 현재 기본값 -1이다.
4. **펄스 출력은 차동(AM26C31)만 있다.** 24V 오픈 컬렉터 출력(NFR-005(b), I-026)은 회로에 없다. 펄스 출력단은 ISO7720으로 절연되어 있다(I-030 해당).
5. **서보 인에이블 전용 핀이 없다.** GPIO33은 펄스 출력 인에이블(라인 드라이버 G)이고, servo1axis의 GPIO33 '서보 인에이블'과 의미가 다르다. 서보 ON은 DO1~DO4 중 하나로 보인다(판독 기반 추정).
6. **입출력 점수:** DI 4점, DO 4점(두 축 공용). SRS의 축당 입력 5점 [가정](I-035)과 다르다.
7. **절연 구조:** 필드 24V(ISO1500 측), 5V_ISO(펄스·RS-485), MCU 3V3/ESP_5V가 분리되어 있다. 절연 DC-DC가 2개다.
