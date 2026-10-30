# 경쟁사·대체재 분석 (competitor-analysis)

| 작성 | 작성일(확인일) | 범위 |
|---|---|---|
| competitor-analyst | 2026-10-30 | 국내, 자동차 부품 2·3차 협력사 자동화 설비, 최근 3년. 펄스(Step/Dir) 모션 컨트롤러, PLC 위치결정 모듈, ESP32/STM32 기반 보드 |

> 조사 한계: 이번 세션에서 제조사 공식 사이트(mitsubishielectric.com, deltaww.com, suryasarana.com 등) 접속이 차단되어
> 공식 데이터시트를 직접 열람하지 못했다. 아래 수치는 웹 검색 결과 요약과 판매처·GitHub 페이지에 근거하며,
> 공식 문서로 교차 확인되지 않은 값은 "(간접)" 또는 "미확인"으로 표기했다. 가격은 국내(KRW) 공식 단가를 한 건도 확인하지 못했다.

## 1. 요약

- 비교 제품 7개를 정리했다. 상용 5개(Autonics PMC-1HS/2HS, Mitsubishi FX3U-1PG, Mitsubishi FX5-40SSC-S, LS XGF-PO3H, Delta DVP-10PM), 오픈소스/DIY 2개(FastAccelStepper, FluidNC/Grbl_ESP32).
- 비교표 42셀 중 24셀이 "미확인"이다. 특히 가격과 국내 지원 체계는 전 제품 미확인이다. 가격 기반 차별화 결론은 아직 낼 수 없다.
- 확인된 사실: 상용 펄스 출력 모듈은 200 kpps(FX3U-1PG)에서 4 Mpps(Autonics, 간접)까지 분포한다. ESP32 오픈소스 라이브러리의 문서상 상한은 200 kSteps/s(FastAccelStepper)다.
- 차별화 가설(검증 필요): (1) PLC 없이 단독 동작하는 저가 다축 펄스 컨트롤러, (2) Wi-Fi/웹 설정, (3) 오픈소스 보드 대비 산업 환경 대응(절연·노이즈·24 V 입출력). 모두 [가정]이며 고객 인터뷰와 가격 조사로 검증해야 한다.
- 자동차 부품 2·3차 협력사 설비 수요, 최근 3년 국내 동향은 이번 조사에서 근거를 찾지 못했다(미확인). market-researcher 결과와 대조가 필요하다.

## 2. 선정 기준

1. 출력 방식이 펄스 열(Step/Dir 또는 CW/CCW)이거나 이를 대체하는 서보 통신 모듈일 것(대체재 포함).
2. 국내 설비 현장에서 접할 가능성이 있는 제조사(Autonics, LS ELECTRIC은 국내사, Mitsubishi·Delta는 국내 설비에 널리 쓰이는 PLC 계열) [가정: 국내 점유율 자료는 확인하지 못함].
3. 세 경쟁군을 모두 대표할 것: 독립형 모션 컨트롤러 1개, PLC 위치결정 모듈 4개, ESP32 기반 오픈소스 2개.
4. 웹에서 사양 근거를 1건 이상 찾을 수 있을 것. 근거가 약한 제품은 미확인 셀로 남겼다.
5. 제외: STM32 기반 상용 완제품 보드는 대표 제품을 확인하지 못해 이번에 제외했다(이슈 제기 참조).

## 3. 비교표

확인일은 모두 2026-10-30. "(간접)"은 제조사 공식 문서가 아닌 판매처·검색 요약 근거를 뜻한다.

| 제품 | 분류 | 축 수 | 최대 펄스 주파수 | 인터페이스 | 프로그래밍 방식 | 가격 | 지원 |
|---|---|---|---|---|---|---|---|
| Autonics PMC-1HS/2HS 시리즈 | 독립형 모션 컨트롤러 | 1축 / 2축(축별 독립 프로그램) (간접) [S1] | 4 Mpps (간접) [S1] | 미확인 (모델명에 USB/232 접미가 있으나 사양 미확인) | 미확인 (티칭 유닛 PMC-2TU-232 존재 [S2]) | 미확인 | 미확인 |
| Mitsubishi FX3U-1PG | PLC 위치결정 모듈 | 1축 [S3] | 200 kpps [S3] | 미확인 | 미확인 | 미확인 | 미확인 |
| Mitsubishi FX5-40SSC-S | PLC 모션 모듈(SSCNET III/H) | 4축 [S4] | 해당 없음(펄스 열 출력이 아닌 서보 버스) [S4] | SSCNET III/H [S4] | 미확인 | 미확인 | 미확인 |
| LS ELECTRIC XGF-PO3H | PLC 위치결정 모듈 | 3축(3축 직선 보간, 2축 원호 보간) (간접) [S5] | 미확인 (200 kpps는 피드백 입력값이며 출력값 아님) | 오픈 컬렉터 펄스 출력 (간접) [S5] | 미확인 | 미확인 | 미확인 |
| Delta DVP-10PM | PLC 겸 모션 컨트롤러 | 미확인 | 1 MHz 차동 출력(Delta 문구) [S6]. 다른 요약은 "2축, 500 kHz" 로 상충 | 미확인 | 미확인 | 미확인 | 미확인 |
| FastAccelStepper (ESP32 라이브러리) | 오픈소스 | ESP32 최대 14 스테퍼(6 MCPWM/PCNT + 8 RMT) [S7] | 200 kSteps/s (I2S 방식은 40 kSteps/s) [S7] | 해당 없음(라이브러리, GPIO 출력) | C/C++ (Arduino, ESP-IDF 컴포넌트) [S7][S8] | 미확인 (라이선스·비용 미확인) | GitHub 커뮤니티 [S7] |
| FluidNC / Grbl_ESP32 (ESP32 보드) | 오픈소스 | 최대 6축 (판매 보드 설명) (간접) [S9] | 미확인 (구 Grbl_ESP32 미러에 120,000 steps/s 표기, FluidNC 현행값 미확인) [S9] | USB, Wi-Fi, microSD (판매 보드 설명) (간접) [S9] | G-code, 웹 UI 설정 [S9] | 미확인 | 미확인 (GitHub 프로젝트 존재 [S9]) |

미확인 셀 집계: Autonics 4, FX3U-1PG 4, FX5-40SSC-S 3, XGF-PO3H 4, DVP-10PM 5, FastAccelStepper 1, FluidNC 3. 합계 24 / 42셀.
"해당 없음" 3셀은 미확인에서 제외했다.

## 4. 제품별 분석

사용자 리뷰나 포럼 근거는 이번 조사에서 확보하지 못했다. 아래 강점·약점은 확인된 사양에서 추론한 것이며, 평가가 아닌 사양 차이만 적는다.

### 4.1 Autonics PMC-1HS/2HS
- 강점(사양 근거): 최대 4 Mpps(간접), 24 VDC ±10% 전원, 2축 모델은 축별 독립 프로그램 가능 [S1]. 국내 제조사이므로 국내 지원 가능성이 있다 [가정].
- 약점/한계: 축 수가 최대 2축이다. 인터페이스·가격·지원 정책은 미확인.

### 4.2 Mitsubishi FX3U-1PG
- 강점: 펄스+방향, 정/역 펄스 방식 지원 (판매처 기재) [S3]. PLC 계열 확장으로 쓰이는 구성이다.
- 약점/한계: 1축당 모듈 1개가 필요하다. 200 kpps 상한 [S3]. 가격·지원 미확인.
- 참고: 공식 페이지에서 FX5 계열의 펄스 출력 대안은 FX5-20PG-P(2축, 200 kpps), FX5-20PG-D(2축, 5 Mpps)로 검색 요약에 나왔으나 원문은 미확인이다.

### 4.3 Mitsubishi FX5-40SSC-S
- 강점: 4축 SSCNET III/H 서보 버스로 펄스 열 방식의 배선 부담을 줄이는 대체재 성격이다 [S4].
- 약점/한계: 대응 서보가 SSCNET 호환이어야 한다. 이 프로젝트의 Step/Dir 서보와 직접 호환되지 않는다 [가정: 대상 드라이브가 Step/Dir 입력형일 때]. 가격 미확인.

### 4.4 LS ELECTRIC XGF-PO3H
- 강점: 3축, 직선·원호 보간 지원 (판매처 기재) [S5]. 국내 제조사 PLC(XGT) 생태계.
- 약점/한계: 출력 최대 주파수 미확인. 오픈 컬렉터 출력이라 배선 길이·노이즈 조건은 별도 검토가 필요하다 [가정].

### 4.5 Delta DVP-10PM
- 강점: Delta 문구상 1 MHz 차동 출력 [S6].
- 약점/한계: 축 수가 자료마다 상충하거나 미확인이다. 해소 전에는 비교에 쓰지 않는 편이 안전하다.

### 4.6 FastAccelStepper
- 강점: ESP32 계열 전체에서 200 kSteps/s, 최대 14 스테퍼 동시 구동, ESP-IDF 컴포넌트 제공 [S7][S8]. 우리 제품과 같은 MCU 계열이다.
- 약점/한계: 라이브러리이므로 하드웨어·보호회로·산업용 인증은 사용자 몫이다. 구형 README에는 50,000 steps/s(단일), 37,000(2축) 표기도 남아 있어 문서 간 수치 차이가 있다 [S7].

### 4.7 FluidNC / Grbl_ESP32
- 강점: 6축, Wi-Fi/웹 UI, G-code 입력 [S9]. 취미·소형 CNC 쪽에서 완성된 펌웨어다.
- 약점/한계: CNC(G-code) 중심이라 자동화 설비의 PLC 연동(I/O 시퀀스, 산업 통신)은 별도 구현이 필요할 수 있다 [가정]. 현행 최대 주파수 미확인.

## 5. 차별화 기회와 진입 장벽

모든 항목은 [가정]이며 가격·고객 검증 전이다.

| # | 기회 | 근거 | 검증 방법 |
|---|---|---|---|
| 1 | PLC 위치결정 모듈은 축당 모듈 구성(FX3U-1PG 1축)이고 200 kpps급이다. 다축을 단일 보드로 묶으면 구성 단순화 여지가 있다 | [S3] | 협력사의 실제 서보 축 수와 PLC 보유 여부 인터뷰 |
| 2 | ESP32 라이브러리의 문서상 상한이 200 kSteps/s다. 이 값이 목표 서보의 필요 펄스 주파수를 만족하는지가 핵심 | [S7] | G2에서 서보 드라이브 종류·분해능으로 필요 pps 계산 |
| 3 | 오픈소스(FluidNC)는 CNC용, 상용 PLC 모듈은 PLC 종속이다. 독립형 + Wi-Fi 설정 + 산업 I/O 사이 빈틈이 있을 수 있다 | [S9] 외 | 사용자 인터뷰 |
| 4 | 고주파 영역(수 Mpps)은 Autonics 등이 점유(간접)한다. ESP32 단독으로 이 영역 진입은 불리하다 | [S1] | 요구 주파수 확정 후 판단 |

진입 장벽(추정):
- 신뢰성·안전 인증(KC 등)과 EMC 대응 비용 [가정, 미확인].
- 협력사의 기존 PLC 계열 고착(교육·예비품·유지보수 체계) [가정].
- 국내 지원 체계(기술 문의, A/S)를 상용 제조사 수준으로 갖추기 어려움 [가정].
- ESP32의 실시간성(Wi-Fi 부하 시 펄스 지터)은 검증 전이며 우리 쪽 위험이다 [가정].

## 6. 출처 목록 (확인일 2026-10-30)

| ID | 출처 | 비고 |
|---|---|---|
| S1 | https://www.suryasarana.com/en/PRODUK/Automation-Control/Control/Autonics-Automation-Control-Motion-Controller-PMC-1HS-2HS-Series.html | 판매처 페이지(검색 요약으로만 확인, 직접 열람 실패). 4 Mpps, 1/2축, 24 VDC |
| S2 | https://www.testequity.com/brand/Autonics/category/Data-Acquisition-and-Meters/Environmental-And-Temperature-Testers/Process-Controllers/10283340-PMC-2TU-232 | 티칭 유닛 PMC-2TU-232 (PMC-1HS 계열 호환 기재). 미국 판매가 $219~$241는 국내 가격이 아니어서 표에 넣지 않음 |
| S3 | https://baoanjsc.com.vn/products/positioning-module-mitsubishi-melsec-f-series-FX3U-1PG_317123_en.aspx | 판매처. 200 kHz, 펄스+방향. Mitsubishi 공식 페이지(검색 요약)도 200 kpps 표기 |
| S4 | https://baoanjsc.com.vn/products/simple-motion-modules-mitsubishi-FX5-40SSC-S_317174_en.aspx | 판매처. 4축, SSCNET III/H |
| S5 | https://baoanjsc.com.vn/products/advanced-position-module-ls-xgf-p-series-XGF-PO3H_495539_en.aspx | 판매처. 3축, 보간, 오픈 컬렉터 |
| S6 | https://industrialautomation.delta-emea.com/de/dvp-10pm-1189.htm | Delta EMEA. 1 MHz 차동 출력 문구. 축 수 미기재 |
| S7 | https://github.com/gin66/FastAccelStepper/wiki | 직접 열람. 플랫폼 표 (ESP32 200 kSteps/s, 14 스테퍼, I2S 40 kSteps/s) |
| S8 | https://components.espressif.com/components/gin66/fastaccelstepper/versions/2.0.1/readme | ESP-IDF 컴포넌트 |
| S9 | https://www.cnx-software.com/2025/01/30/fluidnc-compatible-cnc-controller-supports-6-stepper-drivers-web-based-configuration/ 및 https://github.com/bdring/Grbl_ESP32 | 6축, Wi-Fi, 웹 UI, G-code. 120,000 steps/s는 Grbl_ESP32 미러 표기 |

## 7. 이슈 제기

| # | 대상 | 내용 |
|---|---|---|
| 1 | 환경 | mitsubishielectric.com, deltaww.com, suryasarana.com 등이 세션 네트워크 허용 목록 밖이라 열람 실패. 허용 후 재조사하면 미확인 셀 상당수를 줄일 수 있다 |
| 2 | 가격 | 국내 KRW 가격을 확인하지 못함. 국내 대리점·쇼핑몰(예: 오토닉스 공식, 산업용품 쇼핑몰) 확인 필요 |
| 3 | 범위 | STM32 기반 상용 보드와 국내 소규모 모션 컨트롤러 업체, 자동차 부품 2·3차 협력사 수요 근거를 찾지 못함. market-researcher 결과와 대조 필요 |
| 4 | 상충 | Delta DVP-10PM의 축 수·주파수가 자료마다 다름(1 MHz vs 500 kHz). 공식 매뉴얼 확인 전까지 비교에서 보류 |
| 5 | 기존 파일 | 01_market의 market-research.md, summary.md, handoff.md는 이번에 읽거나 수정하지 않음. 이전 시험본 handoff와 내용이 어긋날 수 있다 |

## 8. 단계 전달서 (Handoff): ① → ②

| 작성 | 작성일 | 대상 에이전트 |
|---|---|---|
| competitor-analyst | 2026-10-30 | requirements-analyst |

### 1. 산출물 목록
| 파일 | 내용 요약 | 상태 |
|---|---|---|
| project/01_market/competitor-analysis.md | 경쟁 7종 비교표, 차별화 가설 | 초안(미확인 24셀) |

### 2. 핵심 내용
- 상용 펄스 출력은 200 kpps ~ 4 Mpps(간접). ESP32 라이브러리 문서상 상한 200 kSteps/s.
- 가격·국내 지원은 전 제품 미확인.
- 차별화 가설은 모두 [가정].

### 3. 결정 후보
| # | 후보 내용 | 근거 |
|---|---|---|
| 1 | 목표 최대 펄스 주파수를 200 kpps 이상으로 둘지 G2에서 결정 | S3, S7 |

### 4. 미확인·가정 목록
| # | 항목 | 태그 | 해소 방법 |
|---|---|---|---|
| 1 | 전 제품 KRW 가격 | 미확인 | 국내 대리점 견적 |
| 2 | 공식 데이터시트 사양 | 미확인 | 사이트 접속 허용 후 재조사 |
| 3 | 차별화 기회 1~4, 진입 장벽 | [가정] | 고객 인터뷰 |

### 5. 이슈 제기
위 7절 참조.
