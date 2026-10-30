# status.md — 진행 현황

최종 갱신: 2026-10-30 (메인 세션, 정리: 종료 이슈와 지난 점검 기록은 status-archive.md로 이관)

## 단계 현황

| 단계 | 상태 | 주요 산출물 | 비고 |
|---|---|---|---|
| ① 기획·조사 | 완료 | market-research.md, competitor-analysis.md, summary.md, handoff.md (project/01_market) | 2차 실행. 결정 D-005~D-008 반영 |
| ② 요구사항 | 검토대기 | srs.md v0.4, marketing-review.md, handoff.md (project/02_requirements) | D-009~D-014 반영. G2 승인 요청 가능(조건은 「다음 할 일」 참조) |
| ③ 설계 | 대기 | – | G2 승인 필요 |
| ④ 구현 | 대기 | – | G3 승인 필요 |
| ⑤ 검증 | 대기 | – | |
| ⑥ 문서 | 대기 | – | |
| ⑦ 마케팅 | 대기 | – | G6 승인 필요 |

상태 값: 대기 / 진행중 / 검토대기 / 완료 / 보류

## 게이트 현황

| 게이트 | 상태 | 기록 파일 |
|---|---|---|
| G2 요구사항 확정 | 미실시 | – |
| G3 아키텍처·핵심 부품 | 미실시 | – |
| G6 양산/출시·가격 | 미실시 | – |

## 미해결 이슈

종료된 이슈(해소·대체됨)는 `status-archive.md`에 있다.

| ID | 날짜 | 제기 주체 | 대상 문서 | 내용 | 상태 |
|---|---|---|---|---|---|
| I-001 | 2026-10-30 | market-researcher | 01_market/market-research.md | 국내 협력사 단위 시장 규모·펄스 컨트롤러 수요 미확보. 2차에서도 정량 자료 사실상 없음(MR §9, handoff 미확인 1). 국내 기관 자료와 업계 인터뷰 필요. 목표 고객 확정(G2) 전 사용자 판단 필요 | 미해결(유효, 2차 재확인) |
| I-003 | 2026-10-30 | market-researcher | 01_market/market-research.md | 원문 미열람으로 수치 검증 안 됨. 2차 handoff도 "원문 미열람"으로 명시. 후속 WebFetch 대조 권장 | 미해결(유효) |
| I-005 | 2026-10-30 | competitor-analyst | 01_market/competitor-analysis.md | 네트워크가 공식 페이지 DNS를 막아 WebFetch 실패. 허용 후 재조사 필요 (2차에서도 mitsubishielectric.com, deltaww.com, suryasarana.com 열람 실패. 차단 목록은 I-012) | 미해결(유효) |
| I-006 | 2026-10-30 | competitor-analyst | 01_market/competitor-analysis.md | 국내 KRW 가격 0건 확인(2차 CA §7-2). 대리점·국내 몰 확인 필요 | 미해결(유효) |
| I-007 | 2026-10-30 | competitor-analyst | 01_market/competitor-analysis.md | STM32 기반 상용 펄스 모션 보드 미확인으로 제외(2차 handoff 이슈 4) | 미해결(유효) |
| I-010 | 2026-10-30 | market-researcher / 메인 세션 | 01_market/market-research.md | 출처 9·10·13은 URL 매칭 불확실, 출처 8은 신뢰도 낮아 미사용. 원문 대조 전 근거로 확정 금지 (handoff 이슈 2) | 미해결 |
| I-011 | 2026-10-30 | competitor-analyst | 01_market/competitor-analysis.md | Delta DVP-10PM 축 수·주파수 상충(1 MHz vs 500 kHz), LS XGF-PO3H 출력 주파수 미확인, FastAccelStepper 문서 간 차이(200k vs 50k steps/s). 공식 매뉴얼 확인 전 비교 보류 | 미해결 |
| I-012 | 2026-10-30 | 메인 세션 | 환경 | 차단 도메인: global.yamaha-motor.com, www.motir.go.kr, mitsubishielectric.com, deltaww.com, suryasarana.com (I-005와 연동. 허용 시 미확인 셀 다수 감소) | 미해결 |
| I-013 | 2026-10-30 | market-researcher | 01_market/market-research.md | 시장 규모 출처 간 차이(세계·아태 92억~186억 달러 등) 미해소. 원가·가격 근거로 사용 불가 | 미해결 |
| I-015 | 2026-10-30 | pm-orchestrator | 01_market/competitor-analysis.md | 비교표 42셀 중 24셀 미확인. 가격 우위·차별화 가설 4개는 [가정]이며 검증 불가 | 미해결 |
| I-017 | 2026-10-30 | requirements-analyst | decisions.md (SRS §9 #1, HO2 §5 #1) | §3.1 필요 주파수가 200 kpps를 넘으면 D-007·D-010 재검토를 결정 후보로 올린다. 현재 §3.1 전 셀 미확인(모델 미정, Q1-잔여). 확정은 사용자 권한 | 미해결(③에서 데이터시트 확인 후 판단) |
| I-018 | 2026-10-30 | requirements-analyst | 01_market (SRS §9 #2, HO2 §5 #2) | 국내 경쟁 구성 KRW 가격(최소 3개 제품, 총비용 기준)과 펄스 출력 전기 형식 공식 사양 보강 필요. 없으면 COST-002 불성립(D-011 확정 조건). I-005·I-006·I-012와 연동 | 미해결 |
| I-019 | 2026-10-30 | requirements-analyst | 01_market (SRS §9 #3, HO2 §5 #3) | KC EMC 대상 여부는 국립전파연구원 고시 원문 확인 필요(STD-001). 무선 인증은 v1 제외로 우선순위 낮음 | 미해결 |
| I-020 | 2026-10-30 | requirements-analyst | 02_requirements/srs.md (SRS §9 #4) | 조사 근거 없음 또는 마케팅 [가정] 값 기반 요구사항(FR-008~013, FR-015, FR-018~021, NFR-004·005~007·012~014·017·020~021)은 G2에서 채택 여부 확인 필요 | 미해결(G2 확인 사항) |
| I-021 | 2026-10-30 | requirements-analyst | 01_market/summary.md (SRS §9 #5, HO2 §5 #4) | summary.md는 Wi-Fi/웹 설정을 차별화 기회로 적었으나 SRS는 v1 제외(FR-017). G2 승인 후 summary.md 정정 여부 확인 | 미해결(G2 확인 사항) |
| I-023 | 2026-10-30 | requirements-analyst | 01_market (SRS §9 #7, HO2 §5 #3 후단) | 엔코더 입력 v1 제외 확정(D-012)으로 LS XGF-PO3H 피드백 입력 원문 대조는 v2 자료로만 필요 | 미해결(우선순위 낮음) |
| I-024 | 2026-10-30 | requirements-analyst | 03_design / 01_market (SRS §9 #8, HO2 §5 #5) | 두산은 ①단계 조사 대상이 아니어서 서보 라인업·펄스 열 입력 지원·입력 형식·분해능·최고 회전수 모두 미확인. ③에서 공식 자료 확인 후 대표 모델 후보(Q1-잔여) 제시. 펄스 열 입력형이 없으면 D-009 재검토 후보 | 미해결(③ 착수 시 처리) |
| I-025 | 2026-10-30 | requirements-analyst | 03_design (SRS §9 #9, HO2 §5 #5) | D-010·D-014로 2축 동시 운전 시 축당 ≥ 200 kpps 필수. ESP32 라이브러리 다축 수치 저하(CA §4.6, CON-005) 위험이 커졌으므로 G3 MCU 선정 시 동시 운전 실측 근거 제시 필요 | 미해결(③ 착수 시 처리) |
| I-026 | 2026-10-30 | requirements-analyst | 03_design (HO2 §5 #5) | NFR-005(b) 24 V 오픈 컬렉터 연결이 불가하면 사유와 함께 G3에 보고 | 미해결(③ 착수 시 처리) |

## G2 요청 시 확인 사항 (② 점검 요약)

- 필수 NFR 중 목표값이 미확인인 항목(NFR-003·008·009·011·012·013·015·017)은 G3까지 확정해야 한다(I-017)
- 원가는 조건부다(D-011, I-018)
- SRS §8.1의 [가정] 값과 I-020 채택 여부, I-021 summary 정정 여부
- 남은 확인 질문 8건은 G2 필수가 0건이다(Q1-잔여 > Q11-잔여 > Q14·Q15-잔여 > Q10-잔여·Q18·Q22 > Q20)
- 축 수가 2축으로 확정되어(D-014) SRS v0.4에 반영했다(FR-003, NFR-002 등)

## 다음 할 일

- 사용자: `/gate`로 G2 승인 결정 (승인 전 ③ 시작 금지)
- G2 승인 시 메인 세션이 gates/G2-requirements.md와 decisions.md에 기록
