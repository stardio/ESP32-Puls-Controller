# HMI 패널 조사: 이지뷰(EasyView)·M2I 5/7인치 (SRS v0.8 Q30)

- 작성: parts-selector / 조사일: 2026-10-30 / 상태: 초안 (G3 결정 전, 부품 확정 아님)
- 근거 결정: D-021(표준 지원 메이커 이지뷰·M2I), D-033~D-035(RTU 포트/미실장 구분/RTU 전용 패널 존재), D-038(RTU 전용 패널용 화면 데이터 제작)
- 신뢰도 주의: **이 세션에서 WebFetch가 전 도메인에서 차단되어 제조사 공식 페이지·데이터시트 원문을 열람하지 못했다.** 아래 값은 WebSearch가 반환한 검색 결과 요약(2차)에서 가져왔다. 표에서 "검색요약"은 원문 미열람을 뜻한다. G3 전에 공식 데이터시트로 재확인해야 한다.

## 1. 요약

- 이지뷰는 Weintek의 한국 판매법인으로 소개된다(Komachine 프로필 요약, 2차). 제품은 Weintek 제품(MT8000/cMT 시리즈, EasyBuilder Pro)이다. 공식 확인은 미확인.
- 이지뷰: 이더넷 없이 RS-485만 있는 현행 5/7인치 'RTU 전용' 기종은 **확인하지 못했다(미확인)**. 확인된 5/7인치 기종은 모두 이더넷이 있다.
- M2I: 5/7인치 TOPRW는 이더넷+시리얼 모델이다. XTOP07TW-LD(접미사 -E 없음)가 RTU 전용일 가능성은 있으나 **미확인**(아래 3절).
- 국내 신품 가격은 거의 확인하지 못했다. 확인값은 M2I TOPRW0700WD 1건뿐이고 기준일이 없다.

## 2. 메이커별 5/7인치 기종표

표기: O 있음 / X 없음 / 미확인. RTU 전용 열은 "이더넷 없음 + RS-485 있음"이 확인된 경우만 O.

### 2.1 이지뷰 (Weintek)

| 모델명 | 화면 | 이더넷 | RS-485 | RS-232 | Modbus TCP 클라이언트 / RTU 마스터 | 작화 SW | 국내 가격 | RTU 전용 | 현행 여부·출처 |
|---|---|---|---|---|---|---|---|---|---|
| MT8071iP | 7" 800x480 | O (10/100) | O 2선/4선 (COM3, 검색요약) | O (COM1) | 미확인(Weintek 시리즈 기능으로 알려졌으나 모델별 문서 미열람) | EasyBuilder Pro | 미확인 (해외 USD 345~364, 참고만) | X | 현행 추정, 단종 여부 미확인 [W1][W2] |
| MT8072iP | 7" | O (10/100, 소매점 요약) | O (소매점 요약, 2선/4선 미확인) | O | 미확인 | EasyBuilder Pro | 미확인 | X | 현행 추정 [W3] |
| MT8071iE | 7" 800x480 | O | 시리얼 3포트(상세 미확인) | 미확인 | 미확인 | EasyBuilder Pro | 미확인 (해외 USD 275~282, 참고만) | X | **단종**, 후속 cMT2078X (Wachendorff) [W4] |
| cMT2078X | 7" 800x480 | O (소매점은 2포트로 표기) | 미확인 | 미확인 | 미확인 | EasyBuilder Pro(cMT는 cMT Viewer 병행, 상세 미확인) | 미확인 | X(이더넷 있음) | 현행 추정 [W5] |
| eMT3070B | 7" | O (10/100) | O (COM1, COM3, 이중 절연) | O | 미확인 | EasyBuilder Pro V5.00.01 이상 | 미확인 | X | 현행 추정 [W6] |
| MT6071iP | 7" 800x480 | 미확인 | 미확인 | 미확인 | 미확인 | EasyBuilder Pro | 미확인 | **미확인(후보 조사 필요)** | 포트표 원문 미확인 [W7] |
| 5인치 기종 | – | – | – | – | – | – | – | – | **현행 5인치 기종을 확인하지 못함(미확인)**. MT8050i/MT6050i는 구형으로 보이며 현행 여부 미확인 |
| (참고) MT8051iP | **4.3"** 480x272 | O | O 2선/4선 (COM1, 겸 RS-232) + COM3 2선 | O (COM1) | 미확인 | EasyBuilder Pro | 미확인 | X | **단종**, 후속 MT8052iP (Unipi) [W8][W9] |

### 2.2 M2I

| 모델명 | 화면 | 이더넷 | RS-485 | RS-232 | Modbus TCP 클라이언트 / RTU 마스터 | 작화 SW | 국내 가격 | RTU 전용 | 현행 여부·출처 |
|---|---|---|---|---|---|---|---|---|---|
| TOPRW0500WD | 5" (해상도: 매뉴얼 요약 QVGA 320x240) | O (10/100, RJ45) | O (COM2, RS-232C/422/485 소프트웨어 전환, 2선/4선 구분 미확인) | O (COM1 RS-232C, COM2) | 미확인 | TOP Design Studio(이름은 사전지식. 문서 미확인) | 미확인 | X | 현행 추정 [M1][M2] |
| TOPRW0700WD | 7" (해상도: 매뉴얼 요약 VGA 640x480, 판매처 800x480로 불일치) | O (10/100, RJ45) | O (COM2 동일) | O | 미확인 | TOP Design Studio(미확인) | 266,200원 (풍림 판매페이지, **기준일·VAT 여부 미확인**) | X | 현행 추정 [M1][M3] |
| TOPRE0700WD | 7" | 미확인 | 미확인 | 미확인 | 미확인 | 미확인 | 미확인 | 미확인 | **단종 표기** (plcmart) [M4] |
| XTOP07TW-LD | 7" | **상충**: 판매처 요약은 이더넷 있음, 다른 판매처는 RS-232/RS-485/USB만 기재 | O (COM2, RS-232C/422/485) | O (COM1 RS-232C 전용, 프린터용) | 미확인 | TOP Design Studio(미확인) | 신품 미확인 (중고 145,000원은 제외, 해외 EUR 150 VAT 별도) | **가능성 있음(미확인)** | 현행 여부 미확인 [M5][M6] |
| XTOP07TW-LD-E | 7" | O (10/100, RJ45 1채널, M2I 제품정보 요약) | O | O | 미확인 | 미확인 | 미확인 | X | 현행 여부 미확인 [M6][M7] |
| XTOP05TW-SD | 5" | 미확인 | 미확인 | 미확인 | 미확인 | 미확인 | 미확인 | 미확인 | 자료 없음 |

해석: XTOP 시리즈에서 접미사 -E가 이더넷 옵션을 뜻할 가능성이 있으나 이는 **추정**이다. XTOP07TW-LD(비-E)가 이더넷 없는 RTU 전용인지는 M2I 카탈로그로 확인해야 한다.

## 3. RTU 전용 기종 분류 (과제 2)

| 메이커 | RTU 전용(이더넷 없음, RS-485 있음) 확인 | 후보(미확인) |
|---|---|---|
| 이지뷰 | **없음(확인 못함)**. 조사한 7인치 iP/iE/eMT/cMT는 모두 이더넷 보유 | MT6071iP, 구형 MT6050i/MT6070i 계열: 포트표 미확인 |
| M2I | **확정 불가** | XTOP07TW-LD(비-E), XTOP05TW-SD |

D-035는 RTU 전용 패널이 있다는 사용자 보고이며, 이 조사에서는 모델을 특정하지 못했다. 사용자가 보유한 기종명이 있으면 알려주면 좋다.

## 4. RTU 전용 기종의 RS-485 설정 범위 (과제 3)

RTU 전용 기종이 확정되지 않았으므로 해당 항목은 모두 **미확인**이다. 참고로 얻은 부분값은 다음과 같다.

| 항목 | 값 | 비고 |
|---|---|---|
| 통신 속도 | XTOP07TW-LD 계열 COM2: 2400 bps ~ 115.2 kbps (187.5 kbps는 MPI 직결용) | 검색요약. 속도 개별 목록, RTU 마스터 적용 여부 미확인 [M6] |
| 패리티 | 미확인 | |
| 데이터 비트 / 정지 비트 | 미확인 | |
| 국번 범위 | 미확인 | |
| 최대 국 수 | 미확인 | 물리 한계는 RS-485 트랜시버 사양에 따름 |
| 2선/4선 | XTOP07TW-LD COM2는 RS-422/485 지원으로 기재. 2선/4선 세부 미확인 | |

참고(일반 Modbus 사례일 뿐 지원 범위 아님): Weintek 포럼 사례에서 19200 / 8 / N / 1, RS-485 2선, Modbus RTU 설정이 쓰였다(다른 모델) [W10]. 컨트롤러 펌웨어 설계에 쓰려면 기본값은 19200 8N1 정도로 두되, 지원 범위 목록은 공식 매뉴얼로 확정해야 한다.

## 5. servo1axis의 Weintek MT8051iP 분류 (과제 4)

- 4.3인치(5/7인치 범위 밖)이고 이더넷과 RS-485(COM1 2선/4선, COM3 2선)를 모두 가진 **TCP 가능(이더넷 보유) 기종**으로 분류한다. RTU 전용이 아니다. 근거는 Rusavtomatika 매뉴얼 목록(이더넷, COM1 RS-232/RS-485 2W/4W, COM3 RS-485 2W)이며 이더넷 사양은 MT8051iE 데이터시트를 통한 간접 확인이 포함되어 "추정"이다 [W8][W11].
- Unipi는 단종, 후속 MT8052iP로 표기한다 [W9]. 신규 표준 지원 기종으로는 부적합하다.
- 같은 이지뷰 브랜드(Weintek)이므로 EasyBuilder Pro 화면 자산의 재사용 가능성은 있으나, 해상도(480x272)가 달라 재배치가 필요하다(미검증).

## 6. 표준 지원 후보 추천 (과제 5)

확인 수준이 낮으므로 모두 **추천 후보(결정 후보)**이며 확정이 아니다.

### TCP용 (D-017, D-019)

1. **M2I TOPRW0500WD(5") / TOPRW0700WD(7")**: 이더넷과 RS-485 겸용 시리즈로 TCP와 RTU 모두 한 기종군으로 커버할 수 있다. 국내 메이커라 구매·지원 접근이 용이하다고 판단된다(추정). 7인치 국내 가격 1건(266,200원, 기준일 미확인)이 있다. 단 Modbus TCP 클라이언트 지원은 미확인이므로 확인 후 확정한다.
2. **이지뷰 MT8071iP(7")**: servo1axis와 같은 EasyBuilder Pro 환경이라 기존 HMI 자산을 활용할 수 있다. 이더넷과 RS-485 2선/4선을 보유한다. 현행·단종 여부와 국내 가격은 미확인이며, 대안은 cMT2078X(상세 미확인).

### RTU용 (D-033~D-035, D-038)

1. **M2I XTOP07TW-LD(비-E)**: 이더넷 없이 RS-485/RS-232만 있을 가능성이 있는 유일한 후보다. 단 이더넷 유무가 자료마다 상충하고 현행 여부가 미확인이다. **추천 전 카탈로그 확인 필수.**
2. **이지뷰 RTU 후보: 해당 없음(확인 못함)**. RTU 모델도 이더넷 있는 패널(예: MT8071iP, TOPRW)을 RS-485로 연결하는 방식이 현실적인 대안이다. 이 경우 "RTU 전용 패널이 필요하다"는 D-035 근거가 약해지므로 사용자 판단이 필요하다.

설계 영향: 컨트롤러가 슬레이브이므로 패널이 마스터가 되는 RTU 마스터 기능이 필수다(D-033). 위 후보 모두 RTU 마스터 지원이 미확인이므로 G3 전 확인 항목에 넣는다.

## 7. 미확인 목록

1. 이지뷰의 공식 관계 설명(Weintek 판매법인): 공식 페이지 미열람
2. 이지뷰 현행 5인치 기종
3. 이지뷰·M2I 기종별 Modbus TCP 클라이언트·RTU 마스터 지원 여부
4. RTU 전용 기종 확정 및 RS-485 설정 범위 전부(패리티, 데이터/정지 비트, 국번, 최대 국 수)
5. 국내 신품 가격: 이지뷰 전 기종, M2I 5인치 및 XTOP
6. TOPRW 해상도 불일치(QVGA/VGA/800x480), XTOP07TW-LD 이더넷 유무 상충
7. M2I 작화 소프트웨어 명칭의 공식 확인

## 8. 출처 (확인일 2026-10-30, 모두 검색결과 요약 경유)

- [W1] https://www.wachendorff-prozesstechnik.de/en/product-groups/hmi-iiot-edge-gateways/products/ip-series-hmi-iiot-edge-gateways-wachendorff-prozesstechnik/bediengeraet-hmi-MT8071iP (MT8071iP 사양)
- [W2] https://wolfautomation.com/hmi-7-0-plastic-600mhz-w-ethernet-not-ul , https://www.advantageelectricsupply.com/part/weintek/mt8071ip/4676811 (해외 가격 참고)
- [W3] https://wolfautomation.com/products/hmi/mt8072ip (MT8072iP)
- [W4] https://www.wachendorff-prozesstechnik.de/en/product-groups/archive/discontinued-products/archive-hmi-iiot-edge-gateways/control-device-hmi-MT8071iE (단종, 후속 cMT2078X)
- [W5] https://usedindustrialparts.com/ja-jp/products/weintek-cmt2078x , https://www.damencnc.com/en/cmt2078x-hmi-7-tft-touch-screen-weintek/a6527
- [W6] https://rohtekautomation.com/wp-content/uploads/2024/07/eMT3070B1_Datasheet_ENG.pdf
- [W7] https://www.rusavtomatika.com/manuals/MT8071iP.pdf (MT6071iP 직접 문서 아님)
- [W8] https://www.rusavtomatika.com/manuals/MT8051iP.pdf
- [W9] https://unipi.technology/weintek-mt8051ip-p198 (단종)
- [W10] https://mrplc.com/forums/topic/35611-modbus-addressing-in-weintek-hmi/ 외 포럼 사례(검색요약)
- [W11] https://triplc.com/documents/MT8051iE_Datasheet.pdf (이더넷 간접)
- [W12] https://www.komachine.com/en/companies/easy-view (이지뷰=Weintek 한국 판매법인, 2차), https://www.youtube.com/watch?v=NnYrfCw10kk ("웨인텍 이지뷰 cMT3162X" 제목)
- [M1] https://www.mxon.co.kr/uploaded/board/product/d1_6fc3cb4fcf84daec4f8c1bc46e74d9700.pdf (TOPRW-IO 하드웨어 매뉴얼, 2023.06/2025.12판 언급)
- [M2] https://www.manualslib.com/manual/2814810/M2i-Toprw.html
- [M3] https://ko.accio.com/business/toprw0700wd별점5점 (풍림 266,200원 인용)
- [M4] https://www.plcmart.com/shop/item.php?it_id=1585631472 (TOPRE0700WD 단종 표기)
- [M5] https://usedindustrialparts.com/products/top (XT0P07TW-LD)
- [M6] https://www.mxon.co.kr/uploaded/board/catalog/d1_628aabf52374e65da8f9b8acfcda07bb3.pdf , https://www.manualslib.com/manual/2968196/M2i-Xtop-Series.html
- [M7] https://m2i.com.pl/en/products/WIDE/20 , https://www.plcmart.com/shop/item.php?it_id=1578289541

## 이슈 제기

1. WebFetch가 아래 도메인에서 DNS 실패로 차단되었다. 공식 데이터시트 열람을 위해 허용이 필요하다: www.easyview.co.kr(주소 추정이라 실재 불명), m2i.co.kr / www.m2i.co.kr, www.mxon.co.kr, www.weintek.com, www.manualslib.com, www.komachine.com. (검색 결과에 m2i.co.kr 페이지가 나오므로 도메인은 실재하는 것으로 보인다.)
2. WebSearch는 사용 가능했으나 요약문 수준이라 포트표·설정 범위 원문을 확보하지 못했다.
3. D-035(RTU 전용 패널 존재)의 구체 기종을 이 조사에서 확인하지 못했다. 사용자가 아는 기종명 요청.
4. 이더넷 보유 패널로 RTU 연결이 가능하다면 RTU 모델의 필요 근거(D-035)가 약해질 수 있다. 사용자 판단 사항이며 SRS 개정(I-044)과 연결된다.
