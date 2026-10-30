---
description: ⑤ 시험 계획·테스트 케이스·리스크 점검·결함 분석
argument-hint: [시험 범위 메모(선택)]
---
# ⑤ 검증 단계 실행

메모: $ARGUMENTS

1. `project/04_code/handoff.md`가 있는지 확인한다(없으면 중단). status.md에서 ⑤를 "진행중"으로 바꾼다.
2. `test-designer` 호출 → test-plan.md, test-cases.md, risk-assessment.md, defects.md, handoff.md
3. 커버리지를 확인한다. '필수' SRS ID 중 TC가 없는 항목이 있으면 그 목록을 전달해 test-designer를 재호출한다(1회).
4. Critical 결함이나 고위험 잔여 리스크는 status.md 이슈로 올린다. 수정이 필요하면 해당 모듈의 /phase-4-build를 다시 실행하라고 안내한다.
5. `pm-orchestrator`로 status.md를 갱신하고 보고한다. 실장비 시험 결과는 사용자가 test-cases.md에 기록해야 한다는 점을 안내한다.
