# 부품 후보 비교 (PS-01~18) — ESP32 Puls servo controller

| 문서 | 버전 | 작성 | 기준일 | 기준 입력 |
|---|---|---|---|---|
| PARTS-001 | 0.1 (③ 1차) | parts-selector | 2026-10-30 | decisions.md D-001~D-023, architecture.md ARCH-001 v0.1(§7 PS-01~18), srs.md v0.7(§1, 개정 이력, §8) |

> 이 문서는 후보 비교와 추천안 제안까지다. **부품 확정이 아니다.** 최종 선정은 G3에서 사용자가 한다. 구매·견적 요청은 하지 않았다.
>
> **가격 확인 방식(중요).** 유통사 페이지를 직접 열람하지 못했다(lcsc.com은 세션 네트워크에서 차단되어 WebFetch 실패). 가격은 웹 검색 결과에 노출된 유통사 표기 값(Digi-Key, Mouser, TME, RS, LCSC 등)을 옮긴 것이다. 일부는 유통사 가격을 모은 중개 사이트(findchips, flux.ai, oemstrade)가 인용한 값이라 캐시·구판일 수 있다. 본문에 "확인일 2026-10-30"은 **검색 수행일**이며 유통사 게시일이 아니다. 발주 전 유통사 실시간 페이지 대조가 필요하다.
> 가격 칸 표기: `통화 금액 @수량 (유통사, 출처 번호)`. 같은 부품에서 값이 서로 어긋나면 모두 적고 어긋남을 표시했다. 확인하지 못한 값은 **미확인**이다. 환율은 확인하지 않았다(원화 환산은 bom-draft.md에서 [가정] 표기로만 사용).
> 수급·단종 위험 칸에서 데이터시트로 확인하지 못한 항목은 "미확인"으로 두었다. 일반 상식으로 추정한 내용은 쓰지 않았다.

## 0. 블록별 추천 요약

| 요청 ID | 블록 | 추천안(제안) | 상태 | 가격 | 핵심 근거(SRS ID) |
|---|---|---|---|---|---|
| PS-01 | MCU | STM32F407VGT6 (C1). 차순위 ESP32-WROOM-32E (C2) | 추천안 제시, **G3 전 실측(T-01~T-04) 필요** | 확인 | NFR-001~004, FR-022 |
| PS-02 | 이더넷 PHY | LAN8720A-CP-TR | 추천안 제시 | 확인(변형품 가격, 주의) | FR-022, D-019 |
| PS-03 | RJ45(마그네틱스 내장) | HR911105A (HanRun) | 추천안 제시, 절연 내압 미확인 | 확인(중개 가격) | FR-022, NFR-009 |
| PS-04 | SPI 이더넷 | W5500 | 대안(C4·C7용), 비추천 | 확인 | FR-022, NFR-023 |
| PS-05 | 차동 라인 드라이버 | AM26C31 (쿼드 RS-422) | 추천안 제시, 현행 가격 **미확인** | 미확인(2023년 구판값만 있음) | NFR-005(a), FR-010 |
| PS-06 | 24 V 오픈 컬렉터 | ULN2003A | 조건부(200 kpps 미보증, R-03) | 확인 | NFR-005(b) |
| PS-07 | 절연 DI | TLP293-4 (4채널 포토커플러) | 추천안 제시 | 확인(수량 1 기준) | FR-008~011, NFR-008, NFR-009 |
| PS-08 | 절연 DO | TLP293-4 + ULN2003A | 추천안 제시 | 확인 | FR-011(c) |
| PS-09 | 펄스 경로 절연기 | ISO7741 | 조건부(DC-04 "절연"일 때) | 확인 | NFR-009, NFR-003 |
| PS-10 | 비상정지 게이트 | AM26C31의 enable 핀 직결 | 추천안 제시, 추가 논리 소자 **미확인** | 추가 부품 미확인 | FR-010, NFR-011 |
| PS-11 | RS-485 | SN65HVD72(비절연) | 조건부(DC-06) | 확인 | FR-005 |
| PS-12 | 전원 | TPS54331 + SMBJ24A | 추천안 제시, 절연 DC-DC **미확인** | 일부 확인 | NFR-007, NFR-009 |
| PS-13 | 비휘발 저장 | MCU 내장 플래시 우선, 부족 시 FM24CL64B | 추천안 제시(수명 계산은 미수행) | 확인 | FR-015, FR-021 |
| PS-14 | 기구 | Phoenix Contact ME MAX 22.5 계열 + MC 1.5/6-ST-5.08 | 후보 제시, 1축·2축 수납 적합성 **미확인** | 확인(GBP, 소량 소매) | NFR-017 |
| PS-15 | 대표 드라이브 사양 | 조사 결과(§PS-15) | 미쓰비시·LS 부분 확인(제3자), 두산 **미확인** | 해당 없음 | §3.1, NFR-005 |
| PS-16 | 외부 펄스 IC | NOVA MCX512 (예비) | 예비, 사양 일부만 확인 | 확인(1건) | NFR-001~004 |
| PS-17 | HMI 패널(참고) | 이지뷰·M2I 모델 **미확인** | 조사 실패 | 미확인 | CON-012, FR-024 |
| PS-18 | 실측 평가보드 | STM32F407G-DISC1 외, ESP32 이더넷 보드 **미확인** | 일부 확인 | STM32측 확인 | §6 |

집계: 추천안 제시 10건(PS-01,02,03,05,07,08,10,12,13,14), 조건부 3건(PS-06,09,11), 대안·예비 2건(PS-04,16), 참고 조사 3건(PS-15,17,18). 조사 실패·미확인이 남은 건은 PS-05(가격), PS-10(논리 소자), PS-15(두산), PS-17(전체), PS-18(ESP32 보드)이다.

---

## PS-01 MCU

| 항목 | M4 STM32F407VGT6 (추천) | M1 ESP32-WROOM-32E-N4 (차순위) | M5 STM32H563(참고) |
|---|---|---|---|
| 핵심 사양 | Cortex-M4 168 MHz, LQFP-100(Digi-Key 품목명), 이더넷 MAC 내장, 타이머 최대 17개·168 MHz [S6, 아키텍처 §4.1] | ESP32 모듈, 이더넷 MAC 내장(RMII), RMT 8채널 [아키텍처 §4.1 S1~S3]. 모듈 플래시 4 MB(품번 N4 표기) | Cortex-M33 250 MHz, 이더넷 MAC, 타이머 최대 24개 [S8] |
| 단가 | USD 11.87 @1, 9.31 @10, **7.97 @100**, 7.27 @1,000 (Digi-Key STM32F407VGT6TR 페이지, [P1]). 다른 값: Mouser 9.12 @10, 7.72 @540(릴, [P1] 스니펫), LCSC 4.39 @1([P1] 스니펫, 재고·변형 미확인), Digi-Key 비TR 품목 6.89 @1(중개 사이트 인용, 구판 의심) | USD 3.0097 @1 (Digi-Key, findchips 인용 [P2]), TME 2.34~3.03(수량별, [P2]), Mouser 5.59 @1(flux.ai 인용) 및 2.99 @1(oemstrade, 재고 0) — **유통사 간 불일치** | LQFP-144 ZIT6: TME 8.64(재고 57) / 9.09 @1(다른 페이지), RS UK GBP 6.68 [P3]. 100핀 VIT6 가격 **미확인**. Mouser 4.32는 VGT6(다른 품목)이라 비교 불가 |
| 수급·단종 | 재고는 Digi-Key 스냅샷에 노출되나 수치 미확인. 단종 예고·장기 공급 프로그램 해당 여부 **미확인** | Digi-Key 재고 640(findchips 스냅샷), LCSC·Mouser 재고 다수로 표기. 단종 위험 **미확인** | RS 재고 722(스냅샷). 단종 위험 미확인 |
| 인증 | 무선 없음. 동작 온도 0~55 ℃ 충족 여부는 품번 접미(T6=산업용 범위로 알려짐)가 있으나 **이 조사에서 데이터시트 열람 안 함 → 미확인** | 무선 모듈. 무선 미사용 시 인증 요구는 R-05로 **미확인** | 미확인 |
| 평가 | 정지를 하드웨어가 결정(P4). 2축 동시 200 kpps+TCP 부하는 **미확인, 실측 필요** | RMT 보충 마감 160~640 µs(아키텍처 산술). 2축 동시 여부 미확인 | 성능 여유. 원가 불리 가능(가격 위 참조) |

추천: STM32F407VGT6. 근거: NFR-004(펄스 누락·추가 0)를 하드웨어 정지로 보장하는 구조(ARCH §4.4), 무선 인증 부담 없음. 반대 근거: 단가는 ESP32 모듈의 약 2.6배(@100 대 @1 혼합 비교이므로 정확한 비율은 미확인). D-001 프로젝트명에 ESP32가 있어 DC-01 확인 필요.
M6(MAC 미탑재 STM32+W5500): 대상 품목이 정해지지 않아 비교 불가(**미확인**). M3 ESP32-P4: 가격·RMT 모두 미조사.

## PS-02 이더넷 PHY (RMII, 10/100)

| 항목 | LAN8720A-CP-TR (추천) | KSZ8081RNA | LAN8710A |
|---|---|---|---|
| 사양 | RMII, 24핀 QFN, 3.3 V, 100 Mbps [P4] | RMII, 24핀 QFN, 3.3 V [P5] | RMII 계열(패키지·온도 미확인) |
| 단가 | Mouser LAN8720A-CP-TR-**ABC** USD 1.03 @1, 0.86 @25, **0.825 @100** (재고 17,746, 중개 사이트 인용, 정확히 -TR 품번이 아닌 변형품) [P4]. LCSC 0.4492(기준가, 수량 미확인). TME EUR 0.71 @5,000 | Future Electronics USD 0.84 @1,000, 5.64 @160(두 값이 어긋남, **불일치**), RS UK GBP 0.824 @10(재고 없음 표기, 변형 KSZ8081RNACA) [P5] | Future Electronics LAN8710A-EZC-TR USD 0.845 @5,000(최소 수량), 별도 표기 1.34 [P5] |
| 수급 | Mouser 재고 17,746(스냅샷) | Future 재고 92,000 표기. RS 재고 없음 | Future 재고 5,000 표기 |
| 온도 | 소스 불일치: 표준품 0~+85 ℃ 다수, 0~+70 ℃ 1건. 산업용 LAN8720Ai −40~+85 ℃ [P4]. **현행 Microchip 데이터시트 대조 필요**. 0~55 ℃ 요구(NFR-012)는 어느 쪽이어도 범위 안 | 미확인 | 미확인 |
| 비고 | STM32F407 + PHY 검증 사례는 이 조사에서 미확인(PS-02 요청 항목) | 〃 | 〃 |

추천: LAN8720A. 근거: 가격 확인된 후보 중 수량 100 기준 최저 수준이고 RMII 지원. 산업용 Ai 변형(Mouser USD 1.27 @1, flux.ai 인용)은 온도 여유가 필요할 때의 대안.

## PS-03 RJ45 (마그네틱스 내장)

| 항목 | HR911105A (HanRun) | 비고 |
|---|---|---|
| 사양 | 10/100 마그젝, LED 포함(LCSC 분류 RJ45-LED) [P6] | 절연 내압 값은 이 조사에서 데이터시트 미열람 → **미확인** |
| 단가 | HQ Online USD 1.18218 @1, 1.03441 @10, **0.96052 @30+**. LCSC 두 스냅샷이 불일치: 1.22 @1 → 0.80 @992+, 또는 1.48 @1 → 0.90 @992+, 기준가 0.9003(재고 81,331). Flux.ai 1.4814 [P6] | 중개·재판매 가격 혼재 |
| 수급 | LCSC 재고 81,331~96K(스냅샷) | 산업 온도 등급 **미확인** |
| 후보 2 | 대체 후보 HCTL HC911105A-M305-A(LCSC C3021364) — 가격·사양 **미확인** | 2번째 후보는 존재만 확인 |

추천: HR911105A. 근거: 유통 재고·가격 정보가 가장 풍부함. 산업 온도·절연 내압은 데이터시트 확인이 선행되어야 한다(NFR-009, R-06).

## PS-04 SPI 이더넷 컨트롤러 (대안)

| 항목 | W5500 |
|---|---|
| 사양 | 하드웨어 TCP/IP, 소켓 8, SPI 80 MHz, 버퍼 32 KB, MAC·PHY 내장, LQFP48 [S9, P7] |
| 단가 | TME(US) USD 2.66 @10, 2.18 @250. LCSC 2.80 @1 → 1.80 @1,000+. HQ Online 2.34 @1 → 1.90 @30+. Future 2.54 헤드라인, 1.68 @2,500(최소 2,500). Digi-Key(지역 페이지) USD 6.83 — **유통사 간 편차 큼** [P7] |
| 수급 | Digi-Key 일부 지역 품절·백오더 표기. 그 외 재고 3,860(HQ) |
| 후보 비교 | 같은 기능의 다른 SPI 이더넷 IC는 이 조사에서 비교하지 못함(**미확인**) |

평가: PHY(LAN8720A @100 USD 0.825) + 내장 MAC 방식 대비 W5500은 @10~30 기준 약 2배 이상. C1 구성에서는 불필요. C7(MAC 미탑재 STM32) 또는 C4(ESP32-S3) 선택 시에만 필요.

## PS-05 차동 라인 드라이버 (펄스 출력, enable 핀)

| 항목 | AM26C31 (TI, 쿼드 RS-422, 추천) | SN65HVD72 (TI, 1채널 RS-485 드라이버 겸용, 대안) | AM26LS31 계열 |
|---|---|---|---|
| 사양 | 4 송신기, 수신기 없음, 최대 신호율 10 Mbps, 상승·하강 시간 5(typ)/10(max) ns, 공통 enable(G, /G) [P8] | 1채널 트랜시버, DE/RE 핀. 최대 속도는 이 조사에서 미열람 → **미확인** | 미조사 → **미확인** |
| 필요 수량 | 2축 PULSE/DIR 4채널 → **1개**, 1축 2채널 → **1개** (동일) | 2축 4개, 1축 2개 | 미확인 |
| 단가 | 현행 가격 **미확인**. Digi-Key AM26C31IDBR USD 1.43은 flux.ai 페이지(2023-07 갱신)의 구판값이므로 **확인값으로 쓰지 않음**(참고용) [P8] | Digi-Key SN65HVD72DR USD 2.52 @1, 1.871 @10, **1.5328 @100**, 1.355 @1,000 (Digi-Reel, 릴비 USD 7) [P9] | **미확인** |
| 수급 | Digi-Key AM26C31QDR 상태 Active 표기 | Digi-Key 재고 표기 있으나 수치 미확인 | 미확인 |
| enable로 비상정지 게이트 | 가능(공통 enable 입력, 전파 지연 enable/disable 약 10~19 ns [P8]) | DE 핀 사용 가능 | 미확인 |

추천: AM26C31. 근거: 한 개로 2축 4채널을 처리하고 enable로 E-stop 차단 가능, 200 kpps(주기 5 µs) 대비 신호율 여유. 단 현행 가격은 확인하지 못했으므로 bom-draft에서 **미확인**으로 둔다. 데이터시트의 출력 전압·종단 조건과 대표 드라이브 차동 입력의 정합은 미검토.

## PS-06 24 V 오픈 컬렉터 출력단

| 항목 | ULN2003ADR (TI) | 이산 MOSFET/포토커플러 구성 |
|---|---|---|
| 사양 | 7채널 NPN 달링턴, 50 V, 500 mA/ch [P10]. 스위칭 속도·200 kpps 가능 여부 **미확인** | 미조사 → **미확인** |
| 단가 | Digi-Key USD 0.77 @1, 0.48 @10, **0.31 @100**, 0.21 @1,000, 릴 0.19 @2,500 (재고 약 209,000, 리드타임 12주 표기) [P10] | **미확인** |
| 수급 | 재고 충분 표기 | 미확인 |

평가: 가격은 확인되나 R-03(24 V OC 출력의 200 kpps 가능 여부)이 실측 전이므로 **조건부**. 달링턴은 포화 전압·턴오프 지연이 있어 200 kpps에서 파형 왜곡 위험이 있다고 판단되나(설계 추정, 데이터시트 미확인) T-06으로 검증해야 한다. 불가 시 NFR-005(b)에 따라 G3 보고.

## PS-07 절연 디지털 입력

| 항목 | TLP293-4 (Toshiba, 4채널, 추천) | PC817 / EL817 (1채널) | 디지털 절연기 + 전류 제한단 |
|---|---|---|---|
| 사양 | 4채널 포토커플러. 판정 전압·응답 시간·절연 내압 값은 **미확인(데이터시트 미열람)** | 1채널, DIP-4/SMD. 4채널 대신 4개 필요 | 예: ADuM1401(4채널, 2,500 Vrms), ISO7741(5.7 kV 변형) |
| 단가 | Digi-Key USD 1.25 @1, Mouser 1.68 @1 (중개 사이트 인용, 재고: Digi-Key 1,624 / Mouser 76,424) [P11] | PC817AS 계열 USD 0.0382 @10 ~ 0.0204 @4,000(출처 공급처 불명 중개 사이트), Mouser 인용 PC817 0.2390 @2,000. **품목·공급처가 섞여 있어 비교 불가**. EL817M(A)-V는 Mouser 재고 0, Avnet USD 0.14 [P11] | ADuM1401BRWZ-RL: Digi-Key USD 7.88 @1, 6.10 @10, 5.19 @100, 4.67 @1,000 [P12]. ISO7741FQDWWRQ1: USD 4.44 @1, 2.81 @100, 2.51 @1,000 [P12] |
| 수량 | 1축 모델 입력 6점 → 2개(8채널), 2축 입력 11점 → 3개(12채널) | 입력 6/11개 | 채널 방향(3/1) 때문에 DI 전용 대응 미검토 |

추천: TLP293-4. 근거: 가격 확인되고 4채널로 부품 수가 적음. 근거 약점: 가격이 수량 1 기준이고, 100 단위 가격은 미확인. 디지털 절연기는 가격이 4~6배라 입력용으로는 원가 불리(설계 판단).

## PS-08 절연 디지털 출력 (서보 ON)

구성 제안: 포토커플러(TLP293-4 1채널 사용) + ULN2003ADR로 24 V 싱크 출력. 가격은 PS-07, PS-06 값 그대로(TLP293-4 USD 1.25 @1, ULN2003ADR 0.31 @100). 출력 전류·절연 내압은 데이터시트 미열람 → **미확인**. 서보 ON 출력 형식(싱크/소스)은 대표 드라이브 확인(PS-15) 후 정한다. 대체 후보(릴레이, 솔리드 스테이트)는 **미조사**.

## PS-09 펄스 경로 절연기 (조건부: DC-04 "절연"일 때만)

| 항목 | ISO7741 (TI) | ADuM1401 (ADI) |
|---|---|---|
| 사양 | 4채널(정방향 3 / 역방향 1 구성으로 알려짐, 데이터시트 미열람 → **방향 구성 미확인**), 5.7 kV(FQDWWRQ1 변형, 차량용 Q1 등급 표기) [P12] | 4채널, 2,500 Vrms, 25 kV/µs CMTI. 변형 B 10 Mbps, C 90 Mbps [P12] |
| 단가 | USD 4.44 @1, **2.81 @100**, 2.51 @1,000 (Digi-Key, 6주 리드타임) | USD 7.88 @1, 5.19 @100 (B 변형, 재고 1,281) |
| 필요 수량 | 펄스·방향 2축 4신호 → 1개, 1축 2신호 → 1개. 방향 구성에 따라 달라질 수 있음 | 동일 |

평가: ISO7741이 가격 우위. 단 이 블록은 DC-04 결정 전에는 BOM에 넣지 않고 조건부 소계로만 둔다. 절연 시 출력단 전원도 절연이 필요(PWR-B, PS-12, 가격 미확인).

## PS-10 비상정지 게이트

추천: AM26C31의 공통 enable 핀을 E-stop 절연 입력으로 직결(PS-05 데이터). 추가 논리 소자(필요 시)의 부품·가격은 **미확인**(이번 조사 범위에서 미조사). 전파 지연은 AM26C31 enable/disable 10~19 ns 수준 표기[P8]에 절연기·논리 지연이 더해지므로 NFR-011 값은 시제 실측(T-05) 전에는 **미확인**.

## PS-11 RS-485 (조건부: DC-06)

| 항목 | SN65HVD72DR (TI, 비절연, 추천) | ADM2587E (ADI, 절연형) |
|---|---|---|
| 사양 | 3.3 V 구동(Digi-Key 설명 3~3.6 V 표기), SOIC-8 [P9] | 절연형 RS-485 트랜시버, isoPower [P13] |
| 단가 | Digi-Key USD 2.52 @1, 1.871 @10, **1.5328 @100**, 1.355 @1,000, 릴 1.311 @2,500 | LCSC USD 7.6704 @1, Mouser 9.75 @100(중개 인용), Digi-Key 스페인 EUR 12.92 @1 — **불일치**, ADI 1,000개 기준가 USD 6.18 표기 [P13] |
| 수급 | 재고 표기 | Digi-Key 리드타임 20주 표기(태국 페이지) |

평가: 비절연 SN65HVD72는 DC-06에서 RTU 실장 시 추가되는 단가가 작다. 절연형은 4~6배. 절연 필요 여부는 NFR-009 해석에 달림(미결).

## PS-12 전원

| 항목 | TPS54331DR (강압, 추천) | SMBJ24A (TVS) | 절연 DC-DC(PWR-B) |
|---|---|---|---|
| 사양 | 입력 3.5~28 V, 3 A, 출력 0.8 V부터 가변(저항 분압으로 3.3 V 설정) [P14]. 24 V ±10%(최대 26.4 V)에 28 V 한도라 여유 1.6 V → 서지는 TVS 필수 | 24 V 스탠드오프, 38.9 V 클램프, 600 W [P15] | 후보: ADuM5000, TMA 0505S 등을 검색했으나 **가격·사양 미확인** |
| 단가 | Digi-Key USD 1.58 @1, 1.153 @10, **0.9305 @100**, 0.8138 @1,000, 릴 0.7847 @2,500 (재고 2,339, 리드타임 6주) [P14] | Digi-Key 캐나다 페이지 **CAD** 0.47 @1, 0.30 @10, 0.15 @100 (Littelfuse). 일본 페이지 JPY 53 @1. 통화 상이 [P15] | **미확인** |
| 비고 | 인덕터·다이오드·콘덴서 등 주변 수동소자는 **미확인** | 역접속 보호·퓨즈 소자 **미확인** | DC-04, DC-05 결정에 종속 |

대안 강압 IC(LM2596, MP2315)는 가격을 확인하지 못했다(**미확인**).

## PS-13 비휘발 저장

| 항목 | MCU 내장 플래시 | FM24CL64B-GTR (Infineon FRAM 64 Kbit I2C) | MB85RC256V (Ramxeed FRAM 256 Kbit) |
|---|---|---|---|
| 단가 | 추가 부품 없음 | Digi-Key 미국 USD 2.82 @1, 2.25 @1,000 [P16] | Digi-Key MB85RC256VPNF-G-AMERE2 USD 6.02 @1, 4.89 @500 [P16] |
| 수급 | — | 리드타임 8~20주(페이지별 불일치) | 12주 표기 |
| 평가 | 알람 이력 기록 빈도 대비 쓰기 수명 계산은 **미수행(미확인)**. 플래시 쓰기 중 펄스 엔진 간섭(T-04)도 변수 | 쓰기 수명 부담이 클 때만 | 용량 과다 가능, 가격 불리 |

추천: 내장 플래시 우선, 이력 기록이 잦으면 FM24CL64B. FM24CL64B는 BOM의 조건부 블록에 둔다.

## PS-14 기구 (DIN 케이스, 분리형 단자)

| 항목 | 후보 A Phoenix ME MAX 22.5 계열 | 후보 B Phoenix PCB 하우징(RS 241-6106) | 단자대 Phoenix MC 1.5/6-ST-5.08 |
|---|---|---|---|
| 사양 | 폭 22.5 mm. 예: ME MAX 22.5 U-U1 99×22.5×114.5 mm 폴리아미드, ME MAX 22.5-SF 85×22.5×70.4 mm [P17] | 폴리아미드 DIN 레일 케이스, PCB 슬롯 1.4~1.8 mm [P17] | 6극, 5.08 mm 피치, 플러그 [P18] |
| 단가 | **GBP** 80.30 /10개 박스(RS 858-6559, VAT 제외) = 개당 8.03. ME MAX 22.5-SF는 GBP 68.18 @1(참고가 표시). 상부 하우징만 Building Controls USD 7.61 소매 | **GBP** 74.13 /10개(VAT 제외) = 개당 7.413 | RS UK **GBP** 4.978 @50+. RS 호주 AUD 10.56 @50~200, 9.51 @250+(GST 제외). RS 싱가포르 SGD 8.66 @50~200 |
| 적합성 | 폭 22.5 mm에 2축 PCB(RJ45 + 단자 6개 내외)가 들어가는지 **미확인**. 45 mm급 하우징은 가격 **미확인** | 〃 미확인 | 극 수 [가정]: 아래 BOM 참조 |
| 대체 | Camdenboss, Italtronic: 가격 **미확인** | | 국내 단자대 후보 **미조사** |

평가: 이 두 부품이 BOM에서 가장 큰 불확실 요인이다. RS 가격은 소량 소매이고 브랜드 프리미엄이 크다(국내 조달 단가는 **미확인**). 1축·2축 공용 하우징은 2축 기준 수납 확인이 선행되어야 한다.

## PS-15 대표 서보 드라이브 펄스 입력 사양

| 제조사 | 모델 | 차동(라인 드라이버) 입력 | 오픈 컬렉터 입력 | 출처 수준 |
|---|---|---|---|---|
| 미쓰비시 | MELSERVO-J4 MR-J4-A | 최대 4 Mpulse/s | 200 kpulse/s는 MR-J4-700A 제3자 목록에만 있음. 미쓰비시 페이지에서는 **미확인** | 4 Mpps는 미쓰비시 제품 페이지 표기, 200 kpps는 제3자 [P19] |
| LS ELECTRIC | L7P | 1 Mpps | 200 kpps | 제3자 사양서(ip-systemes 등). 공식 매뉴얼 **미확인** [P20] |
| 두산 | 모델 **미확인** | **미확인** | **미확인** | 검색 결과에 두산 서보 자료 없음. 펄스 열 입력형 라인업 존재 여부도 **미확인** |

시사점: LS L7P의 오픈 컬렉터 한도(200 kpps)는 D-007 기준과 같다. 즉 200 kpps 이상을 쓰려면 **차동 출력이 필요**하고, 24 V OC 출력(NFR-005(b))은 200 kpps가 한계일 수 있다(R-04 관련). 위 수치는 모두 공식 매뉴얼 대조 전이므로 SRS §3.1 반영 전에 확인해야 한다. I/O 형식(알람·완료·서보 ON)은 조사하지 못했다(**미확인**).

## PS-16 외부 펄스 발생 IC (예비)

| 후보 | 사양 | 단가 | 비고 |
|---|---|---|---|
| NOVA electronics MCX512 | 2축, 구동 속도 1 pps~최대 8 Mpps (유통사 목록) [P21] | USD 35.75~ (세금 제외, 수량 조건 미확인) | 단가가 MCU(@100 USD 7.97)의 4배 이상 |
| NOVA MCX314AS | 4축, 최대 4 Mpps(일본어 카탈로그, 2차 출처) [P21] | **미확인** | 2축 요구에 비해 과다 |
| Nippon Pulse PCL6045BL / PCL6025 | PCL6045BL 6.5 Mpps, PCL6025가 2축으로 언급 [P21] | **미확인** | |

평가: 원가 9만원 목표에서 칩 단가만 보아도 부담이 크고 인터페이스·가감속 사양은 미조사. 실측에서 MCU 방식이 불합격일 때의 예비안으로만 남긴다.

## PS-17 HMI 패널 (참고)

이지뷰, M2I의 Modbus TCP 클라이언트 지원·기능 코드·동시 접속 제약은 웹 검색에서 해당 제조사 자료를 찾지 못했다(**미확인**). 한국어 제조사 공식 사이트·모델명 직접 조회가 필요하다. 참고로 다른 제조사 7인치 Modbus TCP 패널이 존재함만 확인(Display Visions EA HMI070LM-16ATCS, 유통가 EUR 약 298 VAT 제외) — D-021 대상이 아니므로 비교 대상이 아니다. D-018에 따라 패널은 원가 범위 밖이다.

## PS-18 실측용 평가보드

| 항목 | 가격 | 비고 |
|---|---|---|
| STM32F407G-DISC1 (Discovery) | Botland EUR 34.90, eBay USD 31.99 및 32.77, 인도 소매 INR 2,699 [P22] | 이 보드에 RJ45·PHY가 있는지 이 조사에서 **미확인**. 이더넷 시험에는 PHY 보드 추가가 필요할 수 있음 |
| ESP32 이더넷 평가보드 (ESP32-Ethernet-Kit, WT32-ETH01, Olimex ESP32-EVB) | **미확인**(검색 결과 없음) | |

---

## 이슈 제기

| # | 대상 | 내용 |
|---|---|---|
| 1 | architect / 사용자 | BOM에서 기구(케이스·단자대)가 확인 가능한 최대 항목이며 소량 소매가 기준이라 9만원 초과 위험이 크다. 국내 조달 단가 확인과 단자대 구성(극 수) 재검토가 필요하다 |
| 2 | architect | AM26C31 현행 가격 미확인. 대체 드라이버와 함께 유통사 직접 조회 필요 |
| 3 | 02_requirements | LS L7P 등 오픈 컬렉터 한도 200 kpps는 NFR-005(b)·D-007과 겹친다. 공식 매뉴얼 대조 후 SRS §3.1 반영 여부 판단 필요 |
| 4 | pm-orchestrator | 이번 조사는 유통사 페이지 직접 열람 없이 검색 결과 스니펫으로 수행했다(lcsc.com 차단). 발주 전 가격 재확인이 필요하다 |
| 5 | architect (PS-18) | STM32F407G-DISC1에 이더넷이 없을 가능성이 있어 T-01~T-04 장비 구성 재확인 필요 |

## 출처 (검색일 2026-10-30, 모두 검색 결과 노출 페이지. 직접 열람 아님)

| ID | 내용 | URL |
|---|---|---|
| P1 | STM32F407VGT6 가격 | https://www.digikey.com/en/products/detail/stmicroelectronics/STM32F407VGT6TR/4357578 , https://www.digikey.com/en/products/detail/stmicroelectronics/STM32F407VGT6/2747117 , https://lcsc.com/product-detail/image/STM32F407VGT6_C12345.html , https://www.findchips.com/search/32F407 |
| P2 | ESP32-WROOM-32E-N4 가격 | https://origin-www.findchips.com/search/ESP32-WROOM-32E-N4 , https://www.flux.ai/lcsc/esp32-wroom-32e-n4.md , https://www.oemstrade.com/search/ESP32-WROOM-32E |
| P3 | STM32H563 가격 | https://www.tme.eu/gb/details/stm32h563zit6/st-microcontrollers/stmicroelectronics/ , https://uk.rs-online.com/web/p/microcontrollers/2695189 , https://findchips.com/search/STM32H563VGT6 |
| P4 | LAN8720A | https://www.digipart.com/part/LAN8720Ai-CP-TR , https://www.lcsc.com/product-image/C45223.html , https://www.flux.ai/vasyl/lan8720ai-cp-tr.md , https://origin-www.findchips.com/search/LAN8720A-CP-ABC , https://www.radiolocman.com/datasheet/data.html?di=478005 |
| P5 | KSZ8081RNA, LAN8710A | https://es.farnell.com/microchip/ksz8081rnaia-tr/ethernet-txrx-100mbps-qfn-24/dp/2509803 , https://www.avnet.com/americas/product/microchip/ksz8081rnaia-tr/evolve-3105688/ , https://uk.rs-online.com/web/p/digital-isolator-ics/2153275P |
| P6 | HR911105A | https://datasheet.lcsc.com/lcsc/Zhongshan-HanRun-Elec-HR911105A_C12074.pdf , https://www.lcsc.com/product-image/C12074.html , https://www.hqonline.com/product-detail/rj11---rj45-connectors-hanrun-hr911105a-2500240635 , https://www.flux.ai/jecstronic/hr911105a~ae9.md |
| P7 | W5500 | https://www.tme.com/us/en-us/details/w5500/ethernet-interfaces-integrated-circuits/wiznet/ , https://lcsc.com/product-detail/Ethernet-ICs_WIZNET-W5500_C32843.html , https://www.futureelectronics.com/p/semiconductors--comm-products--switch/w5500-wiznet-2038076 , https://www.digikey.co.nz/en/products/detail/wiznet/W5500/4425702 |
| P8 | AM26C31 | https://digikey.cn/en/products/detail/texas-instruments/AM26C31QDR/1689914 , https://www.flux.ai/adietz12/am26c31db16~fb.md , https://ti.com/document-viewer/AM26C31/datasheet/GUID-CA8AEA8B-43D3-4451-92EE-2813A9345AFC |
| P9 | SN65HVD72 | https://www.digikey.com/es/products/detail/texas-instruments/SN65HVD72DR/3305523 , https://azcus.digikey.com/en/products/detail/texas-instruments/SN65HVD72DR/3305523 |
| P10 | ULN2003ADR | https://www.digikey.com/en/products/detail/texas-instruments/ULN2003ADR/404968 , https://www.tme.eu/lu/en/details/uln2003adr/drivers-integrated-circuits/texas-instruments/ |
| P11 | TLP293-4, PC817, EL817 | https://www.electronicsdatasheets.com/manufacturers/toshiba-electronics-europe/parts/tlp293-4gbe , https://www.oemstrade.com/search/PC817 , https://www.mouser.com/datasheet/2/143/EL817M_EL817S-1165962.pdf |
| P12 | ADuM1401, ISO7741 | https://www.digikey.com/en/products/detail/analog-devices-inc/ADUM1401BRWZ-RL/655653 , https://www.digikey.com/en/products/detail/analog-devices-inc/ADUM1401CRWZ-RL/655654 , https://azcus.digikey.com/en/products/detail/texas-instruments/ISO7741FQDWWRQ1/11625409 |
| P13 | ADM2587E | https://digikey.cn/en/products/detail/analog-devices-inc/ADM2587EBRWZ/2261070 , https://origin-www.findchips.com/search/ADM2587EBRWZ , https://fat.lcsc.com/product-detail/image/ADM2587EBRWZ-REEL7_C12081.html |
| P14 | TPS54331 | https://azcus.digikey.com/en/products/detail/texas-instruments/TPS54331DR/2047963 |
| P15 | SMBJ24A | https://punchouttest.digikey.ca/en/products/detail/littelfuse-inc/SMBJ24A/285994 , https://www.digikey.jp/en/products/detail/bourns-inc/SMBJ24A/2254117 |
| P16 | FRAM | https://www.digikey.com/en/products/detail/infineon-technologies/FM24CL64B-GTR/3788937 , https://www.digikey.com/en/products/detail/ramxeed/MB85RC256VPNF-G-AMERE2/25609752 |
| P17 | DIN 케이스 | https://uk.rs-online.com/web/p/din-rail-enclosures/8586559 , https://uk.rs-online.com/p/din-rail-enclosures/8586606 , https://uk.rs-online.com/web/p/din-rail-enclosures/2416106 , https://www.building-controls.com/products/phx-2707495 |
| P18 | 단자대 | https://uk.rs-online.com/web/p/pluggable-terminal-blocks/8817550 , https://au.rs-online.com/web/p/pluggable-terminal-blocks/8817550 , https://sg.rs-online.com/web/p/pluggable-terminal-blocks/8817550 |
| P19 | MR-J4-A | https://www.mitsubishielectric.com/fa/products/drv/servo/pmerit/mr_j4/amp/machine.html , https://baoanjsc.com.vn/products/mr-j4-a-servo-amplifier-mitsubishi-MR-J4-200A_318118_en.aspx , https://plc-direct.com/products/mr-j4-200a |
| P20 | LS L7P | https://www.ip-systemes.com/media/3415/8536-fiche-technique-l7p.pdf , https://cdn.automationdirect.com/static/specs/l7pdrives.pdf , https://click2electro.com/ls-electric-2/ls-electric-drives/ls-electric-servo-drives/ls-electric-l7p-servo-drive/ |
| P21 | 펄스 IC | https://www.umezawa.co.jp/base/wp-content/uploads/2020/05/CTLG3030.pdf , https://www.electronicdesign.com/technologies/components/electromechanical/article/21754237/motion-control-chip-features-65-mpulse-s-output |
| P22 | 평가보드 | https://botland.store/619-stm32-microcontrollers , https://www.ebay.com/itm/STM32F4DISCOVERY-STM32F407VGT6-Cortex-m4-Development-Board-MCU-ST-LINK-V2-SWD/133200493463 |
| S6~S9 | architecture.md §9의 S6(STM32F407), S8(STM32H563), S9(W5500) 재인용 | architecture.md §9 참조 |
