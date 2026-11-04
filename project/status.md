# status.md — 진행 현황

최종 갱신: 2026-11-03 (메인 세션, 로컬 PC 전환·ESP-IDF 빌드 점검 결과 반영)

## 단계 현황

| 단계 | 상태 | 주요 산출물 | 비고 |
|---|---|---|---|
| ① 기획·조사 | 완료 | market-research.md, competitor-analysis.md, summary.md, handoff.md (project/01_market) | 2차 실행. 결정 D-005~D-008 반영 |
| ② 요구사항 | 완료 | srs.md v0.10, marketing-review.md, handoff.md (project/02_requirements) | D-009~D-022 반영. G2 필수 질문 0건. G2 승인 요청 가능(조건은 「다음 할 일」 참조) |
| ③ 설계 | 진행중 | design-baseline-v0.4.md(v0.4.1, 현행 기준선), reviews/baseline-v0.4-review.md, bom-schematic.md v0.4, power-budget.md, schematic-reconciliation.md | 2축 실측(D-067) 후 G3. 시험 펌웨어 핀 수정 → 사용자 PC 실측 |
| ④ 구현 | 진행중(예외) | 04_code/test-fw(시험, 리뷰 4차·T-15 추가), 04_code/firmware(2축 제품, 리뷰 2차 통과) | D-025·D-071 예외. ESP-IDF v5.5.3 실제 빌드 6벌 OK(2026-11-03 로컬 PC, 프로젝트 경고 0, 앱 파티션 여유 50~52%). 2026-11-04 칩 v1.3 확인 → 리비전 v1.x·360 MHz로 재빌드(D-084), 시험 펌웨어 플래시·부팅·Modbus TCP 응답 확인. 현재 보드에는 벤치 빌드(2axis_tcp + sdkconfig.bench, D-085) 탑재, 핀 변경(D-087: X 펄스 22, RS-485 TX 14) 반영, PC 웹 UI(svtool web)로 X·Y 이동 확인. 남은 것: 보드 실측(T-03~T-09·T-11·T-12·T-15), Minor n-01·n-02 |
| ⑤ 검증 | 대기 | – | |
| ⑥ 문서 | 대기 | – | |
| ⑦ 마케팅 | 진행중(사전 마케팅) | marketing-plan, pricing-proposal v0.2, catalog, proposal, handoff (project/07_marketing) | D-031 예외: G6 전 초안·제안만. 잠정 판매가 1축 149,000원·2축 199,000원 VAT 별도(D-032). 확정은 G6 |

상태 값: 대기 / 진행중 / 검토대기 / 완료 / 보류

## 게이트 현황

| 게이트 | 상태 | 기록 파일 |
|---|---|---|
| G2 요구사항 확정 | 승인 (2026-10-30), v0.8·v0.9 변경 승인 (2026-10-31, 2026-11-01), v0.10 변경 조건부 승인 (2026-11-04, 조건 C1~C6) | gates/G2-requirements*.md |
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
| I-021 | 2026-10-30 | requirements-analyst | 01_market/summary.md (SRS §9 #5, HO2 §5 #4) | summary.md는 Wi-Fi/웹 설정을 차별화 기회로 적었으나 SRS는 v1 제외(FR-017). G2 승인 기록에 정정 여부 결정 없음. 사용자 확인 필요 | 미해결 |
| I-023 | 2026-10-30 | requirements-analyst | 01_market (SRS §9 #7, HO2 §5 #3 후단) | 엔코더 입력 v1 제외 확정(D-012)으로 LS XGF-PO3H 피드백 입력 원문 대조는 v2 자료로만 필요 | 미해결(우선순위 낮음) |
| I-025 | 2026-10-30 | requirements-analyst | 03_design (SRS §9 #9, HO2 §5 #5) | 2축 동시 축당 ≥ 200 kpps 근거. ③은 하드웨어 타이머 방식(C1)을 제시했으나 실측 전(T-01~T-04, HO §4 #3 미확인). 실측 후 해소 판단 | 미해결(실측 대기) |
| I-026 | 2026-10-30 | requirements-analyst / architect / parts-selector | 03_design (HO2 §5 #5, architecture #9, parts #3, HO3 #5) | NFR-005(b) 24 V OC 연결: 드라이브 OC 입력 한도 200 kpps(제3자 자료)로 D-007 하한에 여유 0. 보증 상한 = min(T-06 실측, 200 kpps) 제안(DC-11). SRS 문구 유지 여부 판단 필요. 공식 매뉴얼 대조 후 SRS §3.1 반영 판단 | 미해결(G3 보고, T-06 대기) |
| I-028 | 2026-10-30 | architect / handoff | 파이프라인 (architecture #10, HO3 #2) | 회로도·PCB·기구 설계 담당 단계·에이전트 없음. HW 전용 필수 4건(NFR-007·009·012·017)과 HW 열 항목 담당 미정. 산출물 위치·담당 결정 필요 | 미해결(사용자 결정 필요, G3 전) |
| I-029 | 2026-10-30 | architect / parts-selector | 03_design (R-07, architecture #8, bom #1·3, parts #1, HO3 #7, DC-12·13) | 2축 모델 BOM 확인값 약 91,400원([가정] 환율)으로 9만원 초과 위험. 환율·수량 시나리오(Q11-잔여)와 절감 우선순위 결정 필요. 기구·단자대 국내 조달가 미확인 | 미해결(사용자 결정 필요) |
| I-030 | 2026-10-30 | architect | 02_requirements NFR-009 (architecture #1·2, DC-04) | 펄스 출력이 I/O 절연 대상인지 SRS에 명시 없음(원가 영향 큼). 이더넷 절연 요구값은 IEEE 802.3 원문 확인 전 미확인 | 미해결(G3 결정 필요) |
| I-031 | 2026-10-30 | parts-selector / pm | 03_design (bom #2, parts #2·4) | 가격이 검색 스니펫 기반(lcsc.com 차단). AM26C31 현행가 미확인. 유통사 실시간 재확인 필요 | 미해결 |
| I-032 | 2026-10-30 | architect / parts-selector | 03_design (interface-spec #1·2, R-02, R-09) | AM26C31 전원·3.3 V 호환·enable 기능표, STM32F407 핀·AF·내부 트리거 대조 미완. 결과에 따라 전원 블록·BOM 변경 가능 | 미해결(데이터시트 확인) |
| I-033 | 2026-10-30 | architect | 02_requirements FR-016 (architecture #4, DC-09) | "유선 PC 도구"의 물리 경로를 이더넷으로 충족하는지 확인 필요 | 미해결(G3 결정) |
| I-034 | 2026-10-30 | architect | decisions.md D-001 (architecture #5) | 추천 MCU가 STM32. 프로젝트명 유지 가능하나 제품명·마케팅의 ESP32 표기는 MCU 확정 후 판단 | 미해결(MCU 확정 후) |
| I-035 | 2026-10-30 | architect | 02_requirements §2.5 (architecture #6) | 축당 입력 5점은 [가정]. 범용 입력·알람 리셋 출력 등 추가 I/O 요구 여부 SRS에 없음. PS-15 확인 후 필요 시 SRS 개정 제기 | 미해결 |
| I-036 | 2026-10-30 | architect | 02_requirements FR-010 (architecture #11, interface-spec #3, HO3 #4, DC-08) | 비상정지 후 위치 무효 + 원점복귀 요구는 SRS에 없는 사용 제약. 접점 형식(NC [가정])과 해제 명령 방식 사용자 확인 필요 | 미해결(G3 결정) |
| I-037 | 2026-10-30 | architect | 03_design (interface-spec #4) | MAC 주소 할당(OUI 구매/MAC EEPROM)과 IP 분실 시 초기화 수단 미정. BOM 영향 가능 | 미해결(사용자·⑥) |
| I-038 | 2026-10-30 | architect | 02_requirements FR-023(c)·FR-012 (interface-spec #5·6, HO3 #3·6, DC-16) | "운전에 필요한 파라미터" 목록(interface-spec §5.6) 제안, JOG 유지 상실 시 감속 정지는 FR-012와 별개 [가정]. G3 확인 필요. SRS 미수정 | 미해결(G3 확인) |
| I-039 | 2026-10-30 | parts-selector | 03_design (parts #5, bom #4) | STM32F407G-DISC1에 이더넷 없을 가능성: T-01~T-04 장비 구성 재확인. 하우징 폭 22.5 mm와 2축 PCB·RJ45·단자 배치 기구 적합성(T-09) 검토 필요 | 미해결 |
| I-040 | 2026-10-30 | pm-orchestrator | 03_design/modules | 모듈 25개 파일은 §1~§6(책임·의존성·입출력·제약·완료 기준·미확인·가정)을 갖췄으나 「이슈 제기」 절이 없다(공통 규칙 3과 형식 차이). 모듈 미확인·가정 항목은 이 점검에서 본문 미열람 | 미해결(낮음, 메인 세션 판단) |
| I-042 | 2026-10-30 | 메인 세션 | 02_requirements/srs.md (D-009 관련 문구) | SRS v0.7의 '두산' 표기를 '대우'로 정정해야 한다(D-026). G2 기준선 이후의 정오표 수준 수정이므로 requirements-analyst가 v0.7.1 정오표로 처리 | 미해결 |
| I-043 | 2026-10-30 | 사용자 / 메인 세션 | 03_design (DC-01·03), decisions D-024 | 원가 절감안 A: MCU를 ESP32-P4 ETH 모듈에서 ESP32-S3로, 이더넷을 온보드 IP101에서 W5500(SPI) 모듈로 바꾼다(사용자 추정 약 1만원 절감). 영향: D-024 기준선(P4 1 MHz 운전 실적, D-028)이 그대로 이어지지 않음, S3의 MCPWM·PCNT 2축 자원과 W5500 SPI 처리량·지연 확인 필요. P4 칩 단종 통지(PCN202600801) 위험은 줄어듦. 2026-11-04 시험 보드가 단종 대상 v1.3 칩으로 확인됨(D-084): P4 유지 시 v3.x 보드 전환·재검증 필요 | 보류(D-036: G3 이후 S3 실측·결정. 검토서 cost-reduction-review.md, cost-reduction-bom.md) |
| I-044 | 2026-10-30 | 사용자 / 메인 세션 | 02_requirements (D-017, D-019, FR-022), 03_design | 원가 절감안 B: TCP 모델과 RTU 모델을 옵션으로 분리한다(사용자 추정 약 5천원 절감). 영향: RTU 전용 모델은 G2에서 확정한 'HMI와 Modbus TCP·유선 이더넷'(D-017, D-019)과 충돌하므로 SRS 개정(G2 이후 변경)이 필요. RTU 모델 HMI 연결은 패널=RS-485 RTU 마스터, 컨트롤러=슬레이브(사용자 확인). 기존 RS-485 RTU 마스터(드라이브 측) 용도와의 구분 확인 필요. SKU가 1·2축 × TCP·RTU로 늘어남 | 방식 결정(D-034 공용 PCB 미실장). SRS 개정(D-017·D-019 대체) 필요 |
| I-045 | 2026-11-01 | 사용자 / 메인 세션 | 03_design/hmi/wiring-cards.md | 드라이브 결선 실물 확인 보류(사용자 지시: 하드웨어 결선은 나중에 확인). 남은 항목: 대우 SVON 공통(COM1 7) [추정], MR-J3-A PA13 원문·PD01 자리값, L7S 펄스 형태 파라미터·입력 자동 ON, 두 기종 1 Mpps 원문 대조(D-083 웹 발췌) | 보류(실물 결선 시 확인) |
| I-046 | 2026-11-04 | 사용자 / 메인 세션 | 03_design/cpu-board-options.md (CPU-OPT-001 v0.2) | F1 보드(칩 v1.3) 단종 위험으로 자체 CPU 보드 검토. 권고 A1(P4 v3.x 칩 실장 분리형 4층, F1 헤더 호환). 결정 후보 DC-30~DC-35. architect 리뷰 1차 "조건부 유지" 반영(GPIO54는 v3.x에도 유지 확인, USB는 GPIO24/25 충돌로 USB-UART 브리지 권고) | 결정됨: D(완성 보드 유지, D-086). USB 핀 충돌은 D-087로 해소. 구매 시 칩 리비전 확인 필요 |
| I-047 | 2026-11-04 | architect 리뷰 | 03_design/design-baseline-v0.4.md §3 | "STOP(하드웨어)" 행이 외부 AND 게이트 기준(D-070은 MCPWM fault가 1차), "1 MHz 운전 실적" 문구가 D-078(1 MHz는 파형 확인, 실모터 400 kpps)과 어긋남 | 미해결(기준선 개정 시 반영) |
| I-048 | 2026-11-04 | 사용자 / 메인 세션 | 02_requirements/srs.md (FR-014, CON-010, Q8) | X/Y 직선·원호 보간을 v1에 넣음(D-089, 사용자 지시). SRS v0.9는 직선 보간 v1 범위 밖, 원호 보간 불필요로 되어 있어 SRS 개정(FR-014 범위 안으로, 원호 보간 요구 추가)과 G2 변경 승인이 필요. 구현은 D-071 방식으로 선행 | 해소(G2 변경 조건부 승인 D-090, 2026-11-04. 조건 C1~C6은 gates/G2-requirements-change-v0.10.md) |
| I-049 | 2026-11-04 | 메인 세션(실보드 시험) | 04_code/firmware (D-089 보간) | 리뷰 반영 펌웨어를 올린 직후 첫 시험 묶음(직선 → 반원 → 전체 원 CW) 중 전체 원 시작 시점에 **패닉 리셋 1회**(reset_reason 3, 웹 503). 같은 순서 18회·전체 원 단독 4회 재현 시험은 모두 정상(끝점 정확, 140 s 무리셋). 원인 미확인, 패닉 메시지 미수집 | 해소(2026-11-04, G2 조건 C1, D-091): 원인 = Modbus 클라이언트 태스크(fw_client, 스택 4096) 스택 넘침. 보간 시작의 경로 계산 cos()가 큰 각도(전체 원 CW, −3π)에서 __ieee754_rem_pio2 깊은 경로를 타 HW 스택 가드 패닉. 진단 빌드(sdkconfig.diag)로 재현·백트레이스 확보(평상시에도 여유 132 B). 수정: fw_client 스택 8192, 경로 계산을 motion 태스크로 이동. 이동 후 생긴 시작 시각 역전(즉시 시간 초과) 수정. 재시험 24/24 정상, 패닉 0, 최소 여유 fw_client 5240 B·fw_motion 2672 B |
| I-050 | 2026-11-04 | code-reviewer | 04_code/reviews/interp-sync-review-1.md | 리뷰 1차 "수정 필요": C-01·M-01·M-02·M-03·M-04·M-06 반영, M-05 중 방향 유지 시간·바쁜 대기 반영. **남은 것:** M-05 park 순간 출력 강제 LOW로 HIGH 구간이 잘린 펄스 가능(보간 park는 주로 축 속도 50 Hz 근처) → 오실로스코프로 확인 필요. 펌웨어 단위시험(fw_interp, stream, start_all, 0x0150 블록)은 gcc 환경에서 작성·실행 필요. Minor 9건 미반영. 재리뷰 필요 | 미해결 |
| I-051 | 2026-11-04 | PM 점검 | 02_requirements/srs.md §11 NFR-024 | **ID 충돌:** NFR-024는 이미 [RTU 모델] 시리얼 통신 사양(§4, 줄 299)이고 NFR-003·§2.1·§9가 그 의미로 참조한다. §11의 보간 정확도 NFR-024와 중복. 보간 정확도에 새 ID(예: NFR-025) 부여 필요 | 해소(2026-11-04 SRS v0.10 사전 수정: 보간 정확도 NFR-025로 변경) |
| I-052 | 2026-11-04 | PM 점검 | 02_requirements/srs.md §7 MKT-10, §11 | MKT-10(직선 보간 v1 제외, '반영')이 v0.10과 충돌한 채 그대로다. 마케팅 권고를 사용자 지시(D-089)가 뒤집었음을 MKT-10 행에 '권고 미채택(D-089)'으로 기록하고, 원가·규격 영향 없음 여부(HW 추가 없음, STD 영향)를 §11에 한 줄 적어야 한다 | 해소(2026-11-04 SRS v0.10 사전 수정: MKT-10 권고 미채택 기록, 원가·규격 영향 기재, FR-040 동작 interface-spec §5.13 반영) |

## 다음 할 일

- 사용자 결정 요청(G3 전): DC-10(시험 펌웨어 담당·장비, I-027), 하드웨어 설계 담당(I-028 → DC-33 "사용자"로 정리 제안), 원가 판정 기준(DC-13, I-029)
- G3 전 실측 T-01~T-09 실시 후 결과를 03_design에 반영(architect). 병행: AM26C31·STM32 데이터시트 대조(I-032), 두산·이지뷰·M2I 자료 확인(I-024, Q26)
- 실측 결과가 갖춰지면 PM이 재점검해 G3 승인 요청 가능 여부를 다시 판정
- 권장: 제조사 도메인(mitsubishielectric.com, deltaww.com 등)과 두산·이지뷰·M2I 사이트, 유통사(lcsc.com) 허용
