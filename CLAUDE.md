# ESP32 Puls servo controller — AI 에이전트 제품개발 파이프라인

ESP32 기반 펄스(Step/Dir) 서보 컨트롤러를 기획부터 마케팅까지 단계별로 개발한다.
구조: **단계별 파이프라인 + 전문 서브에이전트 + 공유 폴더(블랙보드) + 사람 승인 게이트**

## 파이프라인

| 단계 | 명령 | 담당 에이전트 | 출력 폴더 | 종료 게이트 |
|---|---|---|---|---|
| ① 기획·조사 | /phase-1-research | market-researcher, competitor-analyst | project/01_market | – |
| ② 요구사항 | /phase-2-requirements | requirements-analyst, marketing-strategist(검토) | project/02_requirements | **G2 요구사항 확정** |
| ③ 설계 | /phase-3-design | architect, parts-selector | project/03_design | **G3 아키텍처·핵심 부품** |
| ④ 구현 | /phase-4-build | coder ↔ code-reviewer | project/04_code | – |
| ⑤ 검증 | /phase-5-verify | test-designer | project/05_test | – |
| ⑥ 문서 | /phase-6-docs | tech-writer | project/06_docs | **G6 양산/출시·가격** |
| ⑦ 마케팅 | /phase-7-marketing | marketing-strategist | project/07_marketing | – |

pm-orchestrator는 단계 전환 판단과 status.md 갱신을 맡는다. 서브에이전트는 다른 서브에이전트를
호출할 수 없으므로, 실행 순서는 메인 세션의 슬래시 명령이 관리한다.

## 공통 규칙 (모든 에이전트 필수)

1. 작업 시작 전 `project/decisions.md`와 직전 단계 산출물(handoff 문서 우선)을 먼저 읽는다.
2. 자기 단계 폴더에만 쓴다. 다른 단계 문서는 수정하지 말고 `project/status.md` 「미해결 이슈」에 남긴다.
   (status.md 갱신 자체는 pm-orchestrator와 메인 세션 몫이다. 다른 에이전트는 산출물 끝의 「이슈 제기」 절에 적는다.)
3. 에이전트 간 전달은 대화가 아니라 파일로 한다. 단계 전달서(`handoff.md`, `templates/handoff.md` 형식)는
   단계 종료 시 메인 세션이 작성하고, 각 에이전트는 자기 산출물 끝에 「이슈 제기」 절만 둔다.
   단, 명령이 handoff 작성을 지정한 에이전트(requirements-analyst, architect, test-designer, tech-writer, marketing-strategist ⑦)는 명령 지시에 따라 해당 에이전트가 handoff.md를 작성한다.
4. 자기 작업에 필요한 문서만 읽는다. 폴더 전체를 읽지 않는다.
5. 근거 없는 수치·사양을 만들지 않는다. 모르면 **"미확인"**으로 표시한다.
6. 조사 내용에는 출처(URL, 문서명, 확인일)를 달고, 가정에는 **[가정]** 태그를 붙인다.
7. 결정이 확정되면 decisions.md에 날짜·결정·근거·출처 단계를 추가한다. 사업 판단과 게이트는 사용자가 확정하고,
   기술 세부 사항은 규칙 9에 따라 권고 기본값으로 확정한다. decisions.md 기록은 메인 세션이 한다.
8. 문서는 한국어로 작성한다. 코드 식별자와 주석은 영어여도 된다.
9. **결정 권한 구분(사용자 지시, 2026-10-31):** 기술 세부 사항(핀 배정, 동작 방식, 회로 보호, 부품 대체, 통신 설정, 시험 조건 등)은 사용자에게 묻지 않는다.
   에이전트가 근거(데이터시트, 계산, 업계 관행)를 붙인 **권고 기본값**을 정하고, 메인 세션이 decisions.md에 '권고 기본값'으로 기록해 진행한다.
   사용자에게 묻는 것은 사업 판단(가격, 출시, 제품 범위, 투자)과 게이트 승인, 그리고 에이전트가 알 수 없는 사실(실물 측정값 등) 중 진행을 실제로 막는 것뿐이다.
   묻더라도 질문은 한 번에 모아 3개 이내로 하고, 각 질문에 권고안을 함께 제시한다. 사용자가 이의를 제기하면 권고 기본값을 바꾼다.

## 운영 모드: 경량 모드 (2026-10-31~, 시험 운영)

토큰 절감을 위해 아래와 같이 운영한다. 효과가 작으면 단독 모드(메인 세션만 사용)로 전환한다.

| 작업 | 담당 |
|---|---|
| 요구사항·설계 문서 작성·수정, decisions.md 기록, status.md 갱신, handoff 작성 | **메인 세션이 직접** |
| 소규모 코드 수정 | 메인 세션 직접. 대규모 신규 구현만 coder |
| **독립 리뷰**(코드, 회로·안전 설계, 요구사항 일관성) | 서브에이전트 유지: code-reviewer, architect(리뷰 모드) |
| 웹 조사(시장·경쟁·부품) | 네트워크 허용 후 필요할 때만. 차단 상태에서는 사용자 실자료 우선 |
| pm-orchestrator | 게이트 직전 점검에만 사용 |

- 서브에이전트를 호출할 때는 읽을 파일과 절을 지정해 필요한 부분만 읽게 한다.
- 사용자 답변은 모아서 한 번에 반영한다(답변마다 재실행 금지).
- status.md에는 미해결 이슈와 현재 상태만 둔다. 종료 이슈는 status-archive.md로 옮긴다.
- 슬래시 명령의 에이전트 호출 순서는 이 표에 맞게 해석한다(작성은 메인 세션, 리뷰는 서브에이전트).

## 승인 게이트 규칙

- G2(②→③), G3(③→④), G6(⑥→⑦)에서는 반드시 멈추고 `/gate`로 사용자 승인을 받는다.
- 승인 기록은 `project/gates/G<n>-<이름>.md`(templates/gate-record.md 형식)에 남기며, 결정 필드가 `승인`이어야 한다.
- 다음 단계 명령은 시작할 때 선행 게이트 기록을 확인한다. `승인`이 아니면 **즉시 중단**하고 사용자에게 알린다.
- 에이전트는 사용자를 대신해 승인 기록을 작성하거나 수정할 수 없다.

## 기술 스택 (decisions.md 기준)

Python(도구·시험 자동화), C(ESP32 ESP-IDF / STM32 HAL). 상세 확정은 ③단계에서 한다.

## 템플릿

`templates/` — srs.md, design-to-code.md, test-case.md, gate-record.md, handoff.md
