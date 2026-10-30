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
7. 결정이 확정되면 decisions.md에 날짜·결정·근거·출처 단계를 추가한다. 확정 권한은 사용자에게 있고,
   에이전트는 "결정 후보"까지만 제안한다. decisions.md 기록은 메인 세션이 사용자 승인 후에 한다.
8. 문서는 한국어로 작성한다. 코드 식별자와 주석은 영어여도 된다.

## 승인 게이트 규칙

- G2(②→③), G3(③→④), G6(⑥→⑦)에서는 반드시 멈추고 `/gate`로 사용자 승인을 받는다.
- 승인 기록은 `project/gates/G<n>-<이름>.md`(templates/gate-record.md 형식)에 남기며, 결정 필드가 `승인`이어야 한다.
- 다음 단계 명령은 시작할 때 선행 게이트 기록을 확인한다. `승인`이 아니면 **즉시 중단**하고 사용자에게 알린다.
- 에이전트는 사용자를 대신해 승인 기록을 작성하거나 수정할 수 없다.

## 기술 스택 (decisions.md 기준)

Python(도구·시험 자동화), C(ESP32 ESP-IDF / STM32 HAL). 상세 확정은 ③단계에서 한다.

## 템플릿

`templates/` — srs.md, design-to-code.md, test-case.md, gate-record.md, handoff.md
