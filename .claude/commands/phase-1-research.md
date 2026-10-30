---
description: ① 시장조사·경쟁사 분석을 병렬 실행하고 요약 보고서를 작성한다
argument-hint: [조사 범위: 지역/고객군/가격대 등]
---
# ① 기획·조사 단계 실행

조사 범위(사용자 입력): $ARGUMENTS
(입력이 비어 있으면 "국내외, 산업용 펄스 출력 모션 제어, 최근 3년"을 기본값으로 쓰고 보고서에 [가정]으로 표기한다.)

1. `project/decisions.md`와 `project/status.md`를 읽는다. status.md에서 ①을 "진행중"으로 바꾼다.
2. **한 번의 응답에서 두 서브에이전트를 병렬로 호출한다.**
   - `market-researcher`: 위 조사 범위와 decisions.md 미확인 항목을 전달한다. 산출물: project/01_market/market-research.md
   - `competitor-analyst`: 위 조사 범위를 전달한다. 산출물: project/01_market/competitor-analysis.md
   - 전달 내용은 범위와 파일 경로만 담는다. 결과는 대화로 받지 말고 파일을 읽는다.
3. 두 파일이 생성됐는지 확인한다(없거나 비어 있으면 해당 에이전트를 1회 재호출하고, 다시 실패하면 이슈로 기록한다).
4. 메인 세션에서 `project/01_market/summary.md`를 작성한다(1쪽 분량).
   - 핵심 발견 5개, 목표 고객 후보, 차별화 기회, 미확인 항목. 각 항목에 원 보고서 절 링크를 단다.
   - 두 보고서가 서로 다른 내용을 말하면 감추지 말고 그대로 병기한다.
5. `templates/handoff.md` 형식으로 `project/01_market/handoff.md`(① → ②)를 작성한다.
6. `pm-orchestrator`를 호출해 완결성을 점검하고 status.md를 갱신한다.
7. 사용자에게 요약 5줄, 결정 후보, 다음 명령(`/phase-2-requirements`)을 보고한다. ②는 자동으로 실행하지 않는다.
