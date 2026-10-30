# 회로도 기준 BOM 재산출 (v0.1) — 2축 캐리어 보드, 4개 모델 원가

| 문서 | 버전 | 작성 | 기준일 | 기준 입력 |
|---|---|---|---|---|
| BOM-002 | 0.1 | parts-selector | 2026-10-30 | reference/2axis-schematic.pdf(1쪽), reference/2axis-schematic-notes.md, bom-draft.md v0.2, cost-reduction-bom.md v0.1, decisions.md D-016·D-018·D-024·D-032~D-036 |

> **초안이며 부품 확정이 아니다.** 구매·견적 요청은 하지 않았다. 기존 파일은 수정하지 않았다.
> **가격 확인 방식.** 가격은 전부 웹 검색 결과 요약에 노출된 값이다(원문 페이지 직접 열람 아님). 기준일 2026-10-30은 검색일이다. 일부 Digi-Key 지역 페이지는 통화 표기가 없어 USD로 보지 않고 아래 표에 "통화 불명"으로 적었다.
> **환율 [가정]**: 1 USD = 1,400원, 1 GBP = 1.3 USD, 1 USD = 7.8 HKD. 근거 있는 값이 아니다.
> **원화 환산은 부품 전자재료비만이다.** PCB 제작비·SMT 조립비는 **미확인**으로 제외했고, 기구(하우징)는 회로도에 없어 별도 줄로만 보인다.
> **판독 한계.** 참조번호·값은 PDF 이미지 판독이며 일부(R 번호 일부, C9, J1/J2 핀 수)는 판독 불확실이다. 원본 재확인 필요.

## 1. 회로도 부품 목록

### 1.1 능동·전원·절연·보호

| 참조번호 | 부품명/값 | 수량 | 블록 | 모델별 미실장 |
|---|---|---|---|---|
| U1, U16 | TI ISO7720FDR (2채널 디지털 절연기) | 2 | 펄스 절연 | U16은 1축 미실장 |
| U7 | TI AM26C31IDR (차동 라인 드라이버 4채널) | 1 | 펄스 출력 | 공통(1축은 2채널만 사용) |
| U2 | Toshiba TLP2361 (고속 포토커플러) | 1 | 펄스 출력 인에이블 | 공통 |
| U3~U6 | Toshiba TLP293 (DO용) | 4 | D/O | 공통 |
| U9~U12 | Toshiba TLP293 (DI용) | 4 | D/I | 공통 |
| Q1~Q4 | MMBT2222A (NPN) | 4 | D/O | 공통 |
| D1~D4 | SS210 (쇼트키 100 V 2 A) | 4 | D/O 플라이백 | 공통 |
| U17 | TI ISO1500 (절연 RS-485) | 1 | RS-485 | TCP 모델 미실장 |
| L2 | Wurth WE 744232090 (공통모드 초크) | 1 | RS-485 | TCP 모델 미실장 |
| D17 | SM712 (RS-485 TVS, SOT-23) | 1 | RS-485 | TCP 모델 미실장 |
| D5 | SMBJ30A (입력 TVS) | 1 | 전원 입력 | 공통 |
| U15 | B2405S-2WR3 (24 V -> 5 V 절연 DC-DC 2 W, ESP_5V) | 1 | 전원 | 공통 |
| U19 | TRACO TMV2405S (24 V -> 5 V 절연 DC-DC 1 W, 5V_ISO) | 1 | 전원 | 공통 |
| F1 | 퓨즈 2 A | 1 | 전원 입력 | 공통 |

### 1.2 커넥터·모듈·헤더

| 참조번호 | 부품 | 극 수 | 수량 | 모델별 미실장 |
|---|---|---|---|---|
| J1, J2 | ESP32-P4 보드 장착 헤더 (J1 "ESP32-P4A", J2 "ESP32-P4B") | 각 20(판독 불확실) | 2 | 공통 |
| J3 | W5500 모듈(SPI, 10핀) | 10 | 1 | RTU 모델 미실장 |
| J4 | PULS1 출력 | 6 | 1 | 공통 |
| J14 | PULS2 출력 | 6 | 1 | 1축 미실장 |
| J6 | D/O | 6 | 1 | 공통 |
| J15 | D/I | 6 | 1 | 공통 |
| J12 | RS-485 | 4 | 1 | TCP 모델 미실장 |
| J5 | 24 V 입력 | 2 | 1 | 공통 |
| JP6 | 종단 점퍼(R68 120 ohm 연결) | 2 | 1 | TCP 모델 미실장 |
| (MCU) | Waveshare ESP32-P4-ETH 보드(기존 조사값) | - | 1 | 공통 |

### 1.3 저항 (총 53개 판독, 일부 번호 판독 불확실)

| 참조번호 | 값 | 수량 | 용도 | 미실장 |
|---|---|---|---|---|
| R1, R2 | 10 k | 2 | W5500 풀업 | RTU |
| R4, R5 | 33 | 2 | U1 입력 직렬 | 공통 |
| R63, R69 | 33 | 2 | U16 입력 직렬 | 1축 |
| R6 | 560 | 1 | U2 LED | 공통 |
| R11 | 10 k | 1 | U2 풀업 | 공통 |
| R7~R10 | 330 | 4 | DO 포토커플러 LED | 공통 |
| R12~R15, R16~R19 | 10 k | 8 | DO 바이어스/풀다운 | 공통 |
| R20~R23 | 0 ohm(판독 메모 기준) | 4 | PULS1 직렬 | 공통 |
| R62, R70, R71, R72 | 0 ohm(판독 메모 기준) | 4 | PULS2 직렬 | 1축 |
| R24~R31 | 2.2 k | 8 | DI 입력 제한 | 공통 |
| R40~R43, R48~R51 | 10 k | 8 | DI 풀업/풀다운 | 공통 |
| R56, R58, R59, R60 | 330 | 4 | DI 출력측 | 공통 |
| R64, R65, R66 | 33 | 3 | ISO1500 신호 직렬 | TCP |
| R67 | 10 k | 1 | ISO1500 | TCP |
| R68 | 120 | 1 | RS-485 종단 | TCP |

### 1.4 커패시터 (총 26개)

| 참조번호 | 값 | 수량 | 미실장 |
|---|---|---|---|
| C3~C6, C9, C11, C33, C34 | 0.1 uF | 8 | C33(5V_ISO측 U17)은 TCP |
| C2, C30 | 0.1 uF | 2 | C2는 RTU, C30은 TCP |
| C21, C25 | 0.1 uF | 2 | 1축 |
| C1, C7, C31 | 4.7 uF | 3 | C1은 RTU, C31은 TCP |
| C8 | 22 uF/50 V | 1 | 공통 |
| C10, C32 | 10 uF/10 V | 2 | 공통 |
| C12~C15, C20, C22, C23, C24 | 0.01 uF | 8 | 공통 |

(0.1 uF 합계 12 = 8 + 2 + 2, 총 12+3+1+2+8 = 26.)

## 2. 단가 조사 (능동·전원·절연)

통화는 표에 적은 대로이며, 원문 직접 열람 전이다.

| 부품 | 채택 단가(USD 환산) | 구간·판매처·통화 | 다른 값 | 상태 |
|---|---|---|---|---|
| ISO7720FDR | 1.29 | @100, Digi-Key 홍콩 HKD 10.0301 / 7.8 [가정 환율] | Digi-Key 캐나다 1.5232(통화 불명), 멕시코 0.9806(통화 불명). 현재 재고 없음/백오더 표기 | 확인(환산 [가정]) |
| AM26C31IDR | 0.58 | @100, Digi-Key 미국(기존 bom-draft, 백오더 12주) | 다른 페이지 0.39~0.60 | 확인(기존) |
| TLP2361 | 0.769 | @5+, Farnell US$ | LCSC 0.2505, Bürklin EUR 0.6807, Digi-Key 1.07(집계, 재고 0) | 확인 |
| TLP293(8개) | 0.59 | Digi-Key 기본형 TLP293(집계 사이트 인용, 수량 구간 **미확인**) | LCSC GB 0.0728, Farnell 0.22, 집계 0.42. 4채널형 TLP293-4는 1.25 @1(기존) | 확인(구간 미확인) |
| MMBT2222A | 0.0371 | @100, Digi-Key 멕시코 USD(UMW/Good-Ark 계열, Digi-Reel 수수료 별도) | 영국 GBP 0.026 | 확인 |
| SS210 | 0.129 | @100, Digi-Key 영국 GBP 0.0993 x 1.3 [가정] | LCSC 0.0114~0.0435 @100 | 확인(환산 [가정]) |
| ISO1500DBQR | 2.37 | @100, Digi-Key 미러(기존, 시점 미확인) | @1 3.78, LCSC @1 1.13 | 확인(기존) |
| WE 744232090 | 1.11 | @100, Digi-Key 멕시코 USD | 스페인 EUR 0.94, 영국 GBP 약 0.97(@1 기준 표기) | 확인 |
| SM712 | 0.3587 | @100, Digi-Key 미국 MCC SM712-TP | Littelfuse SM712-02HTG 0.449, Semtech SM712.TCT 1.03 | 확인(제조사 회로도 미지정) |
| SMBJ30A | 0.105 | @100, Digi-Key 미국 Littelfuse | RS 영국 GBP 0.18 | 확인 |
| B2405S-2WR3 | 2.02(proxy) | Mornsun 원 부품가 **미확인**. Digi-Key 최소 25개 USD 2.024 표기(집계 페이지가 유사 품번 G2405S 표기, 불확실) | LCSC B2405S-2WR3 DEXU 표기 0.7554 @1 | **미확인**([가정] 보수값) |
| TMV 2405S | 5.91 | @100, Digi-Key 브라질 페이지 USD 표기 | @1 6.75, Farnell GBP 4.77 @1, TRC 7.84 | 확인 |
| 퓨즈 2 A | 0.30 | 조사 안 함 [가정] | – | **미확인** |
| W5500 모듈(J3) | 6.21 | 소매, eBay(마켓플레이스) US$ 6.21 | TME 영국 GBP 14.93~17.63 @1(2.54 mm 핀 모듈), 10핀 사양 확인 못함 | 확인(소매, 사양 미확인) |
| 커넥터(J4,J14,J6,J15,J12,J5) | 극당 1.079 | 기존 Phoenix MC 1.5/6-ST-5.08 GBP 4.978 @50+ RS UK를 6극으로 나눈 극당 GBP 0.83 x 1.3 [가정, 선형 비례] | 실제 회로도 커넥터 품번 **미확인** | **미확인**(proxy) |
| J1/J2 헤더 | 0.40 | 조사 안 함 [가정] | – | **미확인** |
| JP6 점퍼 | 0.05 | 조사 안 함 [가정] | – | **미확인** |
| ESP32-P4-ETH 보드 | 12.99 | @1, Waveshare/Amazon(기존 CNX 2025-08-20) | 양산가 미확인, P4NRW32 단종 통지 | 확인(기존) |

수동소자 일괄 **[가정]**: 저항 0.01 USD/개(53개 = 0.53), 0.1 uF·0.01 uF 0.02, 4.7 uF 0.05, 10 uF 0.06, 22 uF/50 V 0.15 USD/개(26개 = 0.82). 조사한 값이 아니다.

## 3. 모델별 BOM 원가 (USD)

블록별 소계(USD):

| 블록 | 구성 | USD |
|---|---|---|
| 공통(1축 기준) | P4 보드 12.99, J1/J2 0.80, U7 0.58, U1 1.29, U2 0.769, TLP293 x8 4.72, Q x4 0.148, D1~D4 0.516, D5 0.105, U15 2.02, U19 5.91, F1 0.30, 커넥터 J4·J6·J15·J5(20극) 21.58, 수동 1.02 | **52.75** |
| 축2 추가(U16 계) | U16 1.29, J14 6극 6.474, R x6·C x2 0.10 | **7.86** |
| 이더넷부(TCP) | J3 6.21, R1·R2·C1·C2 0.09 | **6.30** |
| RS-485부(RTU) | U17 2.37, L2 1.11, D17 0.3587, J12 4극 4.316, JP6 0.05, R x5·C x3 0.14 | **8.34** |

### 3.1 4개 모델 원가표

| 모델 | USD | 원 [가정 1,400원] | 구성 | 9만원(D-016) 대비 |
|---|---|---|---|---|
| 1축 TCP | 59.05 | **82,700** | 공통 + 이더넷부 | -7,300원(여유) |
| 1축 RTU | 61.09 | **85,500** | 공통 + RS-485부 | -4,500원(여유) |
| 2축 TCP | 66.91 | **93,700** | 공통 + 축2 + 이더넷부 | +3,700원 초과 |
| 2축 RTU | 68.96 | **96,500** | 공통 + 축2 + RS-485부 | +6,500원 초과 |
| (참고) 2축 전부 실장 | 75.26 | 105,400 | 모두 실장 | +15,400원 |

위 원가는 PCB 제작·SMT 조립·검사·포장이 **미확인**이라 빠져 있어 실제 원가보다 낮다. 회로도에 없는 하우징을 기존 bom-draft 값(Phoenix ME MAX 22.5, GBP 8.03 = 10.44 USD = 14,600원 [가정])으로 더하면 1축 TCP 97,300 / 1축 RTU 100,100 / 2축 TCP 108,300 / 2축 RTU 111,200원이다(하우징 적합성 미확인).

TCP 모델에서 U19(TMV2405S)는 5V_ISO(펄스 절연측)용이라 미실장 불가이며, RTU 모델에서 보드 내장 IP101은 P4-ETH 보드에 있어 뺄 수 없다(cost-reduction-bom §3.2와 동일).

## 4. 총이익률

판매가는 D-032 잠정(1축 149,000원, 2축 199,000원, VAT 별도, 터치패널 별도). TCP/RTU 가격 차(D-040)는 금액 미정이라 같은 가격을 썼다. 총이익률 = (판매가 - 원가) / 판매가.

| 모델 | 판매가 | 원가 9만원(D-016) 기준 | 이 문서 원가(PCB·SMT·하우징 제외) | 하우징 포함(기존 값 [가정]) |
|---|---|---|---|---|
| 1축 TCP | 149,000 | 39.6% | 44.5% | 34.7% |
| 1축 RTU | 149,000 | 39.6% | 42.6% | 32.8% |
| 2축 TCP | 199,000 | 54.8% | 52.9% | 45.6% |
| 2축 RTU | 199,000 | 54.8% | 51.5% | 44.1% |

PCB·SMT가 미확인이라 "제외" 열의 이익률은 상한이다. 1축은 9만원 기준보다 낮아지고(하우징 포함 시 -5~-7%p), 2축은 9만원이 공통 기준(D-018)이라 2축에 과소였음을 보인다.

## 5. 이전 bom-draft.md 추정과의 차이

| 모델 | bom-draft v0.2 (원) | 이 문서 (원, 하우징 제외) | 하우징 포함 | 차이 요약 |
|---|---|---|---|---|
| 1축 | 76,800 (RTU 제외), 80,200 (RTU 포함) | 82,700 (TCP) / 85,500 (RTU) | 97,300 / 100,100 | bom-draft는 하우징·단자대 GBP 포함 |
| 2축 | 96,700 / 100,000 | 93,700 / 96,500 | 108,300 / 111,200 | 하우징 포함 비교 시 약 +11,600원 |

차이 요인(방향, 크기는 대략):
- **추가**: 5V_ISO용 TMV2405S 5.91 + ESP_5V용 B2405S 2.02 USD(합 약 11,100원), DI/DO 절연단 TLP293 8개 4.72(bom-draft는 TLP293-4 3~4개 3.75~5.00)와 Q·SS210, ISO7720 1~2개(1.29~2.58), W5500 모듈 6.21(bom-draft는 W5500 제외), 수동소자·퓨즈·헤더(bom-draft는 미확인 9건으로 제외).
- **제거**: TPS54331(0.93, 회로도에 없음), ULN2003(0.31), SMBJ24A 대신 SMBJ30A(0.105).
- **단자대**: bom-draft는 Phoenix 6극 GBP 4.978 기준 2~6개, 이 문서는 회로도 극 수(1축 20극, 2축 26극) 기준 극당 선형 환산이라 커넥터가 더 크다. 실제 커넥터 품번이 없어 근거가 약하다.
- 이전 추정은 PCB·수동소자가 미확인 9건으로 빠져 있어 하한이었고, 이번에도 PCB·SMT는 미확인이라 여전히 하한이다.

## 6. 상위 원가 부품 5개와 절감 후보 (2축 TCP 기준, USD)

| 순위 | 부품 | USD | 원 | 절감 후보 (모두 미검증) |
|---|---|---|---|---|
| 1 | 커넥터 6개 계열 J4·J14·J6·J15·J5 (26극 proxy) | 28.05 | 39,300 | 실제 품번 미확인. 극당 단가가 proxy이므로 5.08 mm 범용 단자대 가격 확인이 최우선. 일체형 병합·극 수 축소 |
| 2 | ESP32-P4-ETH 보드 | 12.99 | 18,200 | 양산가 미확인, P4 모듈(F2) 9.99~10.99 + PHY·RJ45, S3 + W5500(A안, D-036으로 G3 이후) |
| 3 | W5500 모듈 J3 (TCP 모델만) | 6.30 | 8,800 | W5500 칩 + 자석 내장 RJ45 3.27 @100(기존). RTU 모델은 미실장 |
| 4 | TMV2405S (U19) | 5.91 | 8,300 | 1 W 5 V 절연 DC-DC로 다른 제조사(B2405S 계열 0.76~2.02) 대체 검토. 5V_ISO 부하 전류 미확인 |
| 5 | TLP293 x8 | 4.72 | 6,600 | 4채널 TLP293-4 2개(1.25 @1 x2 = 2.50)로 약 2.2 USD(3,100원) 절감 가능. 핀 배치 변경 필요 |

차점: ISO1500(2.37)+L2(1.11)+J12(4.32) = RS-485부 8.34, B2405S 2.02(가격 미확인), U1·U16 ISO7720 2.58.

## 7. 가격 미확인 및 기타 한계

- **가격 미확인 12건**: B2405S-2WR3, 퓨즈 F1, J1/J2 헤더, JP6, 커넥터 6종(J4, J5, J6, J12, J14, J15 품번), PCB 제작, SMT 조립. 이 중 B2405S와 커넥터는 [가정] proxy를 합계에 넣었다. 수동소자는 일괄 [가정].
- 수량 구간 혼재(@1, @5+, @100). TLP293은 구간 미확인. ISO7720FDR은 지역 페이지 값이 0.98~1.52로 갈린다. AM26C31IDR·ISO7720FDR은 백오더 표기.
- W5500 모듈은 마켓플레이스 소매가(6.21)이며 TME GBP 14.93이면 +3.8 USD(5,300원) 더 든다. 회로도의 10핀 배치와 맞는 상용 모듈 **미확인**.
- 회로도 기반 부품 사양(커넥터 피치, 퓨즈 정격 외 전압, 수동소자 정밀도·패키지, 0 ohm 값)은 판독 불확실하거나 미확인이다.
- 환율 1,332원/USD(2차 자료)면 USD분이 약 4.9% 감소.

## 8. 이슈 제기

| # | 대상 | 내용 |
|---|---|---|
| 1 | architect / 사용자 | 회로도 커넥터 품번(J4, J5, J6, J12, J14, J15)과 B2405S-2WR3 제조사 확정 필요. 원가 1위 항목이 proxy |
| 2 | 사용자 | D-016 9만원이 PCB·SMT·하우징을 포함하는지 확인 필요. 포함이면 4개 모델 모두 9만원 초과 방향 |
| 3 | architect | 5V_ISO 절연 DC-DC(TMV2405S 1 W)의 부하 전류 검토 필요(AM26C31·ISO7720·ISO1500). 용량 부족 시 부품 변경으로 원가 변동 |
| 4 | 사용자 / pm | D-040 TCP/RTU 가격 차 확정 시 이 원가표 4모델로 이익률 재계산 |
| 5 | pm-orchestrator | 이번에도 검색 요약 값뿐. 원문 열람 허용 후 Digi-Key·LCSC·Mouser 구간 단가 재확인 필요 |

## 9. 출처 (검색일 2026-10-30, 검색 결과 요약 인용)

| 항목 | URL |
|---|---|
| ISO7720FDR Digi-Key 지역 페이지 | https://punchouttest.digikey.hk/en/products/detail/texas-instruments/ISO7720FDR/9745562 , https://punchouttest.digikey.ca/en/products/detail/texas-instruments/ISO7720FDR/7173735 , https://punchouttest.digikey.com.mx/en/products/detail/texas-instruments/ISO7720FDR/7173735 |
| TLP2361 | https://il.farnell.com/toshiba/tlp2361-tpl-e-t/optocplr-digital-15mbps-3-75kv/dp/3872430 , https://www.lcsc.com/product-image/C107626.html?whichImg=0 , https://buerklin.com/en/p/toshiba/optocoupler/tlp2361-e-t/69S6007 |
| TLP293 | https://www.lcsc.com/product-image/C77656.html , https://jp.farnell.com/en-JP/toshiba/tlp293-gb/optocoupler-phototrans-3-75kv/dp/2524318 , https://electronicspecifier.com/product-centre/distributors/digi-key/part/598e384a03aac6a1cd3bf66d |
| TMV 2405S | https://www.digikey.com.br/en/products/detail/traco-power/TMV-2405S/9383541 , https://jp.farnell.com/en-JP/traco-power/tmv-2405s/dc-dc-converter-5v-0-2a/dp/3793970 |
| B2405S-2WR3 | https://origin-www.findchips.com/detail/g2405s-2wr3/MORNSUN , https://www.lcsc.com/product-image/C7461417.html |
| WE 744232090 | https://punchouttest.digikey.com.mx/en/products/detail/w%C3%BCrth-elektronik/744232090/1638873 , https://www.digikey.es/product-detail/en/744232090/732-1469-1-ND/1639481 |
| SM712 | https://azcus.digikey.com/en/products/detail/mcc-micro-commercial-components/SM712-TP/2783868 , https://www.digikey.com/en/products/detail/littelfuse-inc/SM712-02HTG/5019611 |
| SMBJ30A | https://www.digikey.es/en/products/detail/littelfuse-inc/SMBJ30A/286004 , https://uk.rs-online.com/web/p/tvs-diodes/7645616 |
| SS210 | https://www.digikey.co.uk/en/products/detail/evvo/SS210/21407143 , https://www.lcsc.com/product-detail/C85657.html |
| MMBT2222A | https://punchouttest.digikey.com.mx/en/products/detail/good-ark-semiconductor/MMBT2222A/18648895 , https://www.digikey.co.uk/en/products/detail/umw/MMBT2222A/26765910 |
| W5500 모듈 | https://www.ebay.de/itm/W5500-Ethernet-Network-Modules-TCP-IP-51-STM32-SPI-Interface-For-Arduino-M/172216756971 , https://www.tme.eu/dk/en/katalog/ethernet-modules_113720/params/b375_interface/p1443806_spi/ |
| 기존 조사값(재조사 안 함) | AM26C31IDR, ISO1500, P4-ETH, Phoenix 단자대, 하우징, W5500 칩·RJ45: bom-draft.md §8, cost-reduction-bom.md §7 |
