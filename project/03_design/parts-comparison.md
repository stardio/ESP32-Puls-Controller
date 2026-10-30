# 부품 후보 비교 (v0.2, ESP32-P4 기준선) — ESP32 Puls servo controller

| 문서 | 버전 | 작성 | 기준일 | 기준 입력 |
|---|---|---|---|---|
| PARTS-001 | 0.2 (③ 재조사, v0.1 덮어씀) | parts-selector | 2026-10-30 | decisions.md D-001~D-028, architecture.md ARCH-001 v0.3(§7 PS-20, PS-22~29), 기존 PARTS-001 v0.1 |

> 부품 확정이 아니다. 최종 선정은 G3에서 사용자가 한다. 구매·견적 요청은 하지 않았다.
>
> **확인 방식과 한계(중요).** 이번 세션에서는 WebFetch가 전 호스트에서 이름 해석 실패(Waveshare, Digi-Key, TI, Espressif, esp32.com, electrokit, CNX 등 모두 네트워크 허용 목록 밖)로 동작하지 않았다. **모든 값은 웹 검색 결과 요약에 노출된 내용**이며 데이터시트·유통사 페이지 원문을 열람하지 못했다. 따라서 아래 "확인"은 "검색 결과에 해당 값이 표기됨"의 의미이고, 발주 전 원문 대조가 필요하다. 일부 가격은 미러·중개 사이트(Digi-Key punchout 미러, findchips, ciiva, flux.ai) 인용이다.
> 가격 표기: `통화 금액 @수량 (판매처, 출처 번호, 시점)`. 기준일 2026-10-30은 **검색일**이며 게시일이 아니다. 게시 시점을 알 수 있으면 적었다. 모르면 **미확인**.
> 환율은 [가정]이다(bom-draft.md §5).

## 0. 요청별 결과 요약

| 요청 ID | 블록 | 결과 | 가격 | 상태 |
|---|---|---|---|---|
| PS-23 | MCU 탑재 형태 | F1 보드 USD 12.99 @1(2025-08 기사 값). F2 모듈은 Waveshare ESP32-P4-Module USD 9.99~10.99(무선 칩 C6 포함, 이더넷 없음). F3 칩은 **P4NRW32가 단종(EOL) 통지, 후속 P4NRW32X 가격 미확인** | F1 확인(소매 1개) / F2 확인(소매) / F3 미확인 | **중대 이슈**: 칩 리비전 v1.3 단종(R-20, 신규 R-22) |
| PS-24 | IP101 / RJ45 | IP101GRI -40~85 ℃, IP101G/GR/GA 0~70 ℃. 보드 실장 변형 **미확인**. 보드 RJ45 품번·내압 **미확인** | IP101GRI USD 0.462(LCSC 기준가), 1.439 @1 | 부분 확인 |
| PS-25 | 보드 전원 | 보드 입력 5 V(USB-C), PoE는 6핀 헤더 + 별도 PoE 모듈. 헤더의 5 V 핀 사용 가능 여부·소비 전류 **미확인** | TPS54331DR USD 0.9305 @100(재사용 가능 판단, 5 V 설정은 설계 몫) | 부분 확인 |
| PS-26 | RS-485 | ISO1500 활성, 16-SSOP(DBQ), 1 Mbps, -40~125 ℃. 절연 내압·절연 전원 필요 여부 **미확인** | ISO1500DBQR USD 3.78 @1, 약 2.37 @100 | 부분 확인 |
| PS-27 | 핀·전압 | 2×20 헤더, 판매처 표기 잔여 프로그램 GPIO 27. IP101: MDC GPIO31, MDIO GPIO52, 리셋/전원 GPIO51, 클록 GPIO50, PHY 주소 1(ESPHome 설정). 핀 표는 이미지라 **미열람**, GPIO 3.3 V 도메인 **미확인** | 해당 없음 | 미확인 다수 |
| PS-28 | BOM 재집계 | bom-draft.md v0.2 | USD/GBP/CAD 병기 | 완료 |
| PS-29 | HMI(참고) | MT8051iP 단종·후속 MT8052iP는 유통사 2곳 표기(공식 통지 미확인). 이지뷰=Weintek 여부, M2I 사양 **미확인** | MT8072iP(7") EUR 279(VAT 포함) 등 | 부분 확인 |
| PS-20 | 대표 드라이브 | **대우 서보: 한국어·영어 검색 4회 모두 제품 정보 없음 → 전부 미확인.** LS L7C 매뉴얼에서 차동 1 Mpps / OC 200 kpps 확인 | 해당 없음 | 사용자 정보 필요 |
| PS-22 | AM26C31 3.3 V 구동 | AM26C31 VCC 4.5~5.5 V, 입력 VIH 최소 2 V(TTL 호환) → 3.3 V GPIO 직접 구동 가능으로 판단(실측 T-11 필요). 3.3 V 대체 AM26LV31(E) 존재 | AM26C31IDR USD 약 0.58 @100(Digi-Key, 재고 없음·백오더), AM26LV31EIDR 약 0.91 @100 | 확인(검색 요약) |

## PS-23 MCU 탑재 형태 (F1 / F2 / F3)

| 항목 | F1 Waveshare ESP32-P4-ETH 보드 | F2 ESP32-P4 모듈 | F3 ESP32-P4 칩 |
|---|---|---|---|
| 품목 | Waveshare ESP32-P4-ETH [P23]. PoE 모듈 포함 판은 별도 SKU | Waveshare ESP32-P4-Module(16 MB/32 MB 플래시 판, SKU Waveshare-28101/-32604) [P24]. Espressif 공식 P4 모듈은 Digi-Key·Mouser에서 **미확인** | ESP32-P4NRW32(구), ESP32-P4NRW32X·NRW16X(신) [P25] |
| 핵심 사양 | P4NRW32, PSRAM 32 MB, NOR 플래시 32 MB, IP101 RMII, 100M RJ45, 2×20 헤더(잔여 GPIO 27, 판매처 표기), 전원 USB-C 5 V + PoE 6핀 헤더 [P23][P29]. 무선 칩 **미실장으로 판단**(P4 자체 무선 없음). 보드 회로 확인은 **미확인** | 25×25 mm, 캐스텔레이션 1 mm 피치, 3.3 V, P4NRW32X + **ESP32-C6(Wi-Fi 6/BT 5.3) 탑재**, PSRAM 32 MB [P24]. **이더넷 PHY·RJ45 없음 → IP101 등 추가 필요**. 무선 탑재로 R-05(무선 인증) 부담 | QFN104 10×10 mm, 신 판 PSRAM 32 MB/16 MB 인패키지 [P25]. 플래시·PHY·전원·크리스털 별도 |
| 단가 | **USD 12.99** (CNX Software 2025-08-20 기사의 Waveshare/Amazon 가격, 수량 1, 헤더 포함 여부 미확인) [P23]. Waveshare 스토어 표기 범위 USD 12.99~74.99(번들 포함) [P23]. 타 판매처: SEK 229 VAT 포함(Electrokit, 헤더 포함 판), INR 1,525(Hubtronics, 미납땜 판), USD 51.99(Newegg, 제3자로 이상값 의심). 수량 구간 가격 **미확인** | **USD 9.99~10.99**(Waveshare 스토어 표기, 판별 미확인) [P24]. openelab EUR 11.95(품절 표기). CNX는 "온라인 미판매"로 서술(오래된 기사 가능) | 신 판: Mouser 비재고, 최소 주문 2,000, **단가 미확인**. Comet(불가리아) EUR 3.74 @1 / 3.14 @100 표기가 있으나 품명이 "SoC IC RF Dual Core MCU"뿐이라 P4NRW32 여부 불명 → **채택 안 함** [P25] |
| 공급·수명 | **P4NRW32(칩 리비전 v1.3) 단종 통지**: Espressif PCN202600801(2026-05-20)이 v1.3 → v3.2 전환, P4NRW32 EOL, P4NRW32X 권고(부품 번호·리비전·핀 54 정의 상이) [P25]. 보드가 어느 리비전을 쓰는지 **미확인**(Zephyr 문서 v1.3 vs 기존 sdkconfig 최소 v3.1, R-20). 보드 단종·양산 공급 약속 **미확인** | 모듈이 P4NRW32X(v3.x)라는 서술은 있으나 양산 공급·리드타임 **미확인** | LCSC에서 P4NRW32 "단종" 표기 사용자 보고(포럼, 비공식) [P25]. 신 판 리드타임 **미확인** |
| 온도 | 보드 **미확인**. (Waveshare 0~60 ℃ 표기는 10.1" 디스플레이 모델 행이라 해당 안 됨) | 모듈 **미확인** | v3.x 데이터시트: P4NRW16X·32X 모두 -40~85 ℃ [P25]. 구판 v1.3도 동일 범위로 서술(검색 요약) |
| 외형 | 보드 치수 **미확인** | 25×25 mm | 10×10 mm |
| 설계·검증 | 기존 1축 실적 그대로(D-024, D-028) | PHY·RJ45·플래시 외 회로 추가, 이더넷 재검증 | RMII·전원·고속 메모리 설계 필요. 최대 |

판정(제안): 시험(G3 전)과 초기 소량은 F1. **양산은 F1의 공급 안정성이 미확인**이라 F2/F3는 P4NRW32X 기반으로 전환해야 할 수 있다. F2는 모듈 가격(USD 9.99~10.99)에 PHY·RJ45가 더해져 F1(12.99)보다 싸다고 말할 근거가 없다(BOM §4). F3 가격 미확인이므로 비교 불가. 칩 리비전 v1.3 → v3.x 전환 시 sdkconfig 리비전 최소값(3.01)과의 정합과 핀 54 정의 차이를 확인해야 한다(신규 R-22 제안).

## PS-24 이더넷 PHY / RJ45

| 항목 | IP101G 계열 | 비고 |
|---|---|---|
| 온도 | IP101GRI -40~85 ℃, IP101G/GR/GA 0~70 ℃ (데이터시트 발췌, 검색 요약) [P26]. LCSC IP101GA도 0~70 ℃ | 보드에 실장된 변형은 **미확인**. 0~55 ℃(NFR-012)는 두 등급 모두 만족 |
| 단가(F2·F3용) | IP101GRI: LCSC 기준가 USD 0.4620(재고 8,619, 수량 구간 미확인); LCSC "WIZnet" 표기 USD 1.439 @1, 1.134 @30; Digipart USD 1.5503. Digi-Key는 IP101GRI 미취급 표기 [P26] | 값 불일치 큼. 0.462는 기준가라 구간 **미확인** |
| 대안 | LAN8720A USD 0.825 @100(Mouser 변형 품번, v0.1 값 유지) | 보드가 IP101을 쓰므로 F1에서는 무관 |
| RJ45 | 보드 RJ45 품번·절연 내압 **미확인**. F2·F3용은 HR911105A USD 0.96052 @30+(HQ Online, v0.1 값 유지) | R-06 미해소 |

## PS-25 보드 전원

- 전원 입력: USB-C 5 V, PoE는 별도 PoE 모듈을 6핀 헤더에 장착(Waveshare 상품 서술) [P23]. **헤더에서 외부 5 V를 넣는 핀, 소비 전류(2축 운전), 입력 허용 범위는 미확인**(핀 표 이미지만 있어 열람 불가).
- 24 V → 5 V: TPS54331(입력 3.5~28 V, 3 A, 출력 0.8 V부터 가변)은 24 V ±10%(최대 26.4 V) 입력과 5 V 출력 모두 범위 안이라 **재사용 후보**(v0.1 데이터시트 서술 기준). 5 V 출력 설정과 인덕터·다이오드 선정은 미결, 부품 가격 미확인. 단가 USD 0.9305 @100(Digi-Key, v0.1 값).
- 5 V 레일은 AM26C31(VCC 4.5~5.5 V)에도 쓸 수 있어 3.3 V 레일 추가 없이 구성 가능(PS-22). 단 보드 5 V 핀으로 공급할지 별도 레일로 갈지 **미확인**.
- 절연 전원(DC-05): 미확인(v0.1 유지).

## PS-26 RS-485 (ISO1500 vs 대안)

| 항목 | ISO1500DBQ(R) (TI, 기존 [E7]) | SN65HVD72DR (비절연) | ADM2587E (절연, isoPower 내장) |
|---|---|---|---|
| 사양 | 절연형 RS-485/RS-422, 최대 1 Mbps, 16-SSOP(DBQ), 활성, 데이터시트 rev D(2020-02), 수신 failsafe, -40~125 ℃ [P27]. **절연 내압·내장 절연 전원 여부 미확인**(데이터시트 미열람). 기존 RS-485 J12에 "절연 5 V" 핀이 있어(S4 문서) 전원 별도 가능성 있음 | 3~3.6 V, SOIC-8 | v0.1 서술 유지 |
| 단가 | ISO1500DBQR: Digi-Key USD 3.78 @1, 약 2.37 @100, 약 2.11 @1,000(미러 페이지, 시점 미확인, 릴비 USD 7). LCSC USD 1.13 @1(flux.ai, 2024-11-01 갱신, C2671333). Mouser USD 4.61 @1(flux.ai 인용). Arrow 약 4.24. **판매처별 편차 큼** [P27]. ISO1500DBQ(튜브) Digi-Key EUR 4.42 @1, 재고 0 | USD 1.5328 @100 | USD 7.67 @1(LCSC) 등 |
| 비고 | 기존 코드가 이미 사용 → 하드웨어 재사용성 최고 | 절연 불가 | 가장 비쌈 |

판정(제안): DC-06 유지 시 ISO1500. BOM에서는 조건부 블록(RTU)에 USD 2.37 @100로 넣고, 절연 측 전원 비용은 미확인.

## PS-27 핀·전압 (Waveshare ESP32-P4-ETH)

| 항목 | 확인값 | 미확인 |
|---|---|---|
| 헤더 | 2×20, 잔여 프로그램 가능 GPIO 27(판매처 표기) [P23] | 핀별 표(이미지), 스트래핑 핀, 헤더 상 점유 핀 |
| IP101 연결 | MDC GPIO31, MDIO GPIO52, 전원/리셋 GPIO51, 클록 입력 GPIO50, PHY 주소 1(ESPHome 장치 페이지 설정, 검색 요약) [P29]. 기존 코드의 GPIO51·PHY 주소 1과 일치 | RMII 데이터선 핀 전체 |
| 전압 도메인 | P4 모듈 3.3 V 동작(모듈 서술) | **헤더 GPIO가 3.3 V인지 보드 회로로 확인 못함**. 5 V 허용 여부 미확인 |
| 필요 GPIO | 2축 약 26개 [산술, ARCH §4.3] vs 잔여 27 → 여유 1개(R-18). 판매처 표기값이라 실제 가용 수는 **미확인** | |

## PS-22 차동 라인 드라이버 (3.3 V 구동)

| 항목 | AM26C31 (TI, 쿼드 RS-422) | AM26LV31(E) (TI, 3.3 V) |
|---|---|---|
| 전원 | VCC 4.5~5.5 V [P28] | 단일 3.3 V, AM26C31의 핀 호환 저전압 대체로 TI 서술 [P28] |
| 입력 | VIH 최소 2 V, TTL 호환. TI 포럼 답변: 3.3 V 신호로 구동 가능, 3.3 V 전원판은 AM26LV31 권고(검색 요약) [P28] → **ESP32-P4 3.3 V GPIO 직접 구동 가능으로 판단**. GPIO의 VOH 보증값은 P4 데이터시트 미열람 → **미확인**, T-11 파형 시험 필요 | 3.3 V 로직 직결 |
| enable | 공통 enable(G, /G) 기능표는 v0.1 서술(전파 지연 약 10~19 ns). 이번에 원문 재열람 못함 | 동일 계열(핀 호환), 상세 미확인 |
| 단가 | AM26C31IDR: Digi-Key 미국 약 USD 0.58 @100(재고 없음, 백오더, 리드타임 12주 표기); 같은 품번 캐나다 페이지 약 CAD 0.60 @100; 멕시코 페이지 약 USD 0.39 @100(재고·동일 품목 여부 미확인); 중개 인용 Mouser 약 0.39 @100, 재고 14,820 [P28]. v0.1의 USD 1.43(2023 구판)은 폐기 | AM26LV31EIDR: Mouser 약 USD 0.912 @100(재고 4,402), Digi-Key 296-24690-1-ND 약 0.9116 @100(재고 없음 표기) [P28]. 일부 AM26LV31 변형이 단종으로 표기된 판매처가 있어 품번별 상태 **미확인** |
| 판정 | 5 V 레일을 쓰는 설계면 AM26C31(저가). 보드 3.3 V만 쓰면 AM26LV31E | |

BOM 채택값: AM26C31IDR USD 0.58 @100(보수적: 확인된 값 중 최고, 백오더 표기). 출력단 차동 레벨은 5 V 구동, 드라이브 수신 정합은 PS-20 미확인으로 보류.

## PS-20 대표 서보 드라이브

| 제조사 | 모델 | 차동 입력 | OC 입력 | 출처 수준 |
|---|---|---|---|---|
| 대우(D-026) | **미확인** | **미확인** | **미확인** | 검색 4회(한국어 "대우 서보 드라이브", 제조사 후보명 포함 검색, 영어 매뉴얼 검색 2회)에서 대우 서보 드라이브 제품·매뉴얼이 나오지 않음(자일대우버스 등 무관 결과만). 어떤 대우 계열사 제품인지도 **미확인**. servo1axis의 HMI 파일명 `ESP32HMI_대우서보`만 단서 |
| LS ELECTRIC | L7C(매뉴얼, AutomationDirect 호스팅) | 1 Mpps | 200 kpps | 매뉴얼 PDF 검색 요약 [P30]. L7P는 v0.1 제3자 사양서(동일 값). 공식 LS 사이트 대조는 미수행 |
| 미쓰비시 | MELSERVO-J4 MR-J4-A | 최대 4 Mpulse/s | 200 kpps는 제3자 목록뿐(v0.1) | 이번에 재대조하지 못함 |

필요 정보(사용자): 대우 드라이브의 **정확한 모델명(앰프 라벨)** 또는 매뉴얼. 이 정보가 없으면 PS-20, R-04, T-14, DC-04, DC-11은 닫을 수 없다. 참고로 타사 3종 사양(ATO, AASD, SMC 등)에서도 OC는 200 kpps대, 차동은 500 kpps~수 Mpps로 나오나 대우 사양의 근거로 쓰지 않는다.

## PS-29 HMI (참고, 원가 밖)

| 항목 | 확인값 | 미확인 |
|---|---|---|
| MT8051iP | 4.3" 480×272, 유통사(Unipi, Wolf Automation)가 단종·후속 MT8052iP로 표기. Wolf는 MT8052iP를 MT8051iP와 같은 가격 표기, 일부 목록은 "비 UL" 서술 [P31] | Weintek 공식 단종 통지(찾은 EOL 문서는 MT8053iE용이라 무관), 후속 기종과 EasyBuilder 프로젝트 호환 |
| 7" 후보 | MT8072iP: 800×480, 이더넷 10/100. Unipi EUR 279.00(VAT 포함, 제외 230.58), Wolf Automation USD 397.95 [P31]. Modbus TCP 클라이언트 지원은 제품 페이지에서 명시 확인 못함 | 이지뷰 = Weintek 한국 브랜드 여부, 32비트 워드 순서 설정, FC 지원 |
| 10" 참고 | MT8102iP는 단종, 후속 MT8106iP(Unipi 표기) [P31] | |
| M2I | 검색에서 사양·모델 자료 없음 | 전부 미확인 |

패널은 D-018에 따라 원가·공급 범위 밖이다.

## 대안 열: STM32F407안 및 v0.1 유지 항목 (재조사 대상 아님)

기준선 변경으로 아래는 **대안 정보로 유지**한다. 값은 v0.1과 동일(검색 요약 기반, 재확인 안 함).

| 항목 | 값(요약) | 비고 |
|---|---|---|
| MCU STM32F407VGT6 | USD 7.97 @100(Digi-Key TR) [P1]; 대안 ESP32-WROOM-32E-N4 USD 3.0097 @1(불일치 있음) [P2]; STM32H563 USD 8.64~9.09 @1 [P3] | 실적 없음. 전환 조건 ARCH §4.5 |
| 이더넷 PHY LAN8720A | USD 0.825 @100 [P4] | F1에서는 보드 IP101로 대체 |
| RJ45 HR911105A | USD 0.96052 @30+ [P6] | F1에서는 보드 RJ45로 대체. F2·F3에 필요 |
| W5500 | USD 2.66 @10 TME [P7] | 제품 기준선에서 제외(ARCH §2.3) |
| 24 V OC ULN2003ADR | USD 0.31 @100 [P10] | 200 kpps 미보증(R-03) |
| 절연 DI TLP293-4 | USD 1.25 @1 [P11] | 수량 1 기준 |
| 펄스 절연 ISO7741 | USD 2.81 @100 [P12] | DC-04 "절연" 시만 |
| RS-485 SN65HVD72DR | USD 1.5328 @100 [P9] | 비절연 대안 |
| 강압 TPS54331DR / TVS SMBJ24A | USD 0.9305 @100 / CAD 0.15 @100 [P14][P15] | |
| FRAM FM24CL64B | USD 2.82 @1 [P16] | F1은 보드 32 MB NOR + NVS라 불필요로 판단(쓰기 수명 계산 미수행) |
| 기구 Phoenix ME MAX 22.5 / MC 1.5/6-ST-5.08 | GBP 8.03/개 / GBP 4.978 @50+ [P17][P18] | 소매가. 보드 외형 수납 **미확인**(보드 치수 미확인) |
| 외부 펄스 IC MCX512 | USD 35.75~ [P21] | 예비 |
| 실측 평가보드 | STM32F407G-DISC1 EUR 34.90 등 [P22]. ESP32-P4 평가보드는 F1 자체가 해당(USD 12.99) | |

## 이슈 제기

| # | 대상 | 내용 |
|---|---|---|
| 1 | 사용자 / architect | **대우 서보 드라이브 정확한 모델명 또는 매뉴얼 필요**. 검색으로 확인 불가. 1축 1 MHz 검증 드라이브(D-028)도 모델·배선 형식 미확인 |
| 2 | architect / 사용자 | **ESP32-P4NRW32(칩 v1.3) 단종 통지(PCN202600801, 2026-05-20)**. Waveshare ESP32-P4-ETH의 실장 칩 리비전·공급 지속 여부 미확인. 양산 기준 부품을 보드로 할지 P4NRW32X 기반 모듈/칩으로 할지 G3 판단 필요. 신규 위험 R-22 제안 |
| 3 | architect | Waveshare 보드 핀 표(이미지)·헤더 GPIO 전압·5 V 입력 핀·소비 전류·치수·온도 등급 미확인. 네트워크 제한으로 Waveshare 위키/데이터시트(PDF)를 열람하지 못함. 해당 호스트 허용 후 재조사 또는 사용자가 PDF 제공 필요 |
| 4 | pm-orchestrator | 이번 세션은 WebFetch 전면 불가, 검색 요약만 사용. 값 전부 발주 전 원문 대조 필요 |
| 5 | architect | AM26C31 3.3 V 직접 구동은 TTL 호환(VIH 2 V)과 포럼 답변 근거. P4 GPIO VOH 보증값 확인과 T-11 파형 시험으로 닫을 것 |
| 6 | 02_requirements | MT8051iP 단종 여부 공식 확인 안 됨. D-017(5/7") 대상 패널은 MT8072iP(7") 등 후보만 있고 이지뷰 계열 여부 미확인 |
| 7 | architect | F2 모듈(Waveshare)은 ESP32-C6 무선 칩을 포함해 STD-002/R-05 인증 부담이 생길 수 있다 |

## 출처 (검색일 2026-10-30, 전부 검색 결과 요약 인용, 직접 열람 아님)

| ID | 내용 | URL |
|---|---|---|
| P23 | Waveshare ESP32-P4-ETH 가격·사양 | https://www.cnx-software.com/2025/08/20/waveshare-esp32-p4-eth-development-board-supports-ethernet-and-poe/ , https://www.waveshare.com/esp32-p4-eth.htm , https://docs.waveshare.com/ESP32-P4-ETH , https://www.electrokit.com/en/esp32-p4-ethernet-dev-board-100mbps , https://hubtronics.in/esp32-p4-eth , https://www.newegg.com/WaveShare-IoT-Edge-Device-Development-Board/BrandSubCat/ID-161560-3739/Page-6 |
| P24 | Waveshare ESP32-P4-Module | https://www.waveshare.com/product/esp32-related/boards-kits/esp32-p4/esp32-p4-module.htm , https://openelab.io/products/waveshare-esp32-p4-module-core-board-wifi-bluetooth , https://www.cnx-software.com/?p=148378 |
| P25 | ESP32-P4 단종·변형·온도·칩 가격 | https://documentation.espressif.com/en/PCN202600801_ESP32-P4_Chip_Revision_v3.2_Upgrade_Chip_Revision_v1.3_Demand_Collection_and_EOL_Plan_Description.html , https://documentation.espressif.com/esp32-p4-chip-revision-v1.3_datasheet_en.html , https://documentation.espressif.com/esp32-p4_datasheet_en.html , https://www.mouser.co.il/c/unclassified/espressif-systems/ , https://store.comet.bg/en/CatalogueSchukat/Product/3071108 , https://esp32.com/viewtopic.php?p=155374 |
| P26 | IP101 | https://www.lcsc.com/product-image/C703537.html , https://www.digipart.com/part/IP101GRI , https://forum.digikey.com/t/alternative-for-ip101gri-from-ic-plus-corporation/30652 , https://www.alldatasheet.net/html-pdf/1718925/ETC/IP101G-DS-R01/4193/56/IP101G-DS-R01.html |
| P27 | ISO1500 | https://www.ti.com/product/ISO1500 , https://punchouttest.digikey.de/en/products/detail/texas-instruments/ISO1500DBQR/10434767 , https://www.flux.ai/lcsc/iso1500dbqr~c5px.md , https://www.findchips.com/search/iso150 |
| P28 | AM26C31 / AM26LV31 | https://ti.com/document-viewer/AM26C31/datasheet/GUID-00E658C3-78C2-4808-8522-9BBDB60CCA32 , https://www.digikey.com/en/products/detail/texas-instruments/AM26C31IDR/2051231 , https://ciiva.com/part/am26c31idr-13594889 , https://ciiva.com/part/am26lv31eidr-972579 , https://origin-www.findchips.com/search/am26lv31eidr , https://www.ti.com/product/AM26LV31 , https://e2e.ti.com/support/interface-group/interface/f/interface-forum/1443844/am26c31-high-level-output-voltage-voh |
| P29 | Waveshare 보드 IP101 핀 | https://devices.esphome.io/devices/Waveshare-ESP32-P4-ETH/ |
| P30 | LS L7C 매뉴얼 | https://cdn.automationdirect.com/static/manuals/lselectric/l7cmanual.pdf |
| P31 | HMI | https://unipi.technology/weintek-mt8051ip-p198 , https://wolfautomation.com/mt8052ip-hmi-4-3-quot-480-x-272-resolution-tft-lcd , https://www.unipi.technology/weintek-mt8072ip-p199 , https://www.wolfautomation.com/products/hmi/mt8072ip/ , https://www.unipi.technology/weintek-mt8102ip-p200 |
| P1~P22 | v0.1 출처(대안 열·유지 항목) | STM32F407 https://www.digikey.com/en/products/detail/stmicroelectronics/STM32F407VGT6TR/4357578 ; ESP32-WROOM-32E https://origin-www.findchips.com/search/ESP32-WROOM-32E-N4 ; STM32H563 https://www.tme.eu/gb/details/stm32h563zit6/st-microcontrollers/stmicroelectronics/ ; LAN8720A https://www.flux.ai/vasyl/lan8720ai-cp-tr.md ; HR911105A https://www.hqonline.com/product-detail/rj11---rj45-connectors-hanrun-hr911105a-2500240635 ; W5500 https://www.tme.com/us/en-us/details/w5500/ethernet-interfaces-integrated-circuits/wiznet/ ; SN65HVD72 https://www.digikey.com/es/products/detail/texas-instruments/SN65HVD72DR/3305523 ; ULN2003ADR https://www.digikey.com/en/products/detail/texas-instruments/ULN2003ADR/404968 ; TLP293-4 https://www.electronicsdatasheets.com/manufacturers/toshiba-electronics-europe/parts/tlp293-4gbe ; ISO7741/ADuM1401 https://azcus.digikey.com/en/products/detail/texas-instruments/ISO7741FQDWWRQ1/11625409 ; TPS54331 https://azcus.digikey.com/en/products/detail/texas-instruments/TPS54331DR/2047963 ; SMBJ24A https://punchouttest.digikey.ca/en/products/detail/littelfuse-inc/SMBJ24A/285994 ; FRAM https://www.digikey.com/en/products/detail/infineon-technologies/FM24CL64B-GTR/3788937 ; 하우징 https://uk.rs-online.com/web/p/din-rail-enclosures/8586559 ; 단자대 https://uk.rs-online.com/web/p/pluggable-terminal-blocks/8817550 ; MR-J4-A https://www.mitsubishielectric.com/fa/products/drv/servo/pmerit/mr_j4/amp/machine.html ; MCX512 https://www.umezawa.co.jp/base/wp-content/uploads/2020/05/CTLG3030.pdf ; STM32F4 DISC1 https://botland.store/619-stm32-microcontrollers |
