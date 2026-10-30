---
description: ② 요구사항(SRS) 작성 + 마케팅 관점 검토 후 G2 승인 요청 준비
argument-hint: [추가 요구/제약 메모(선택)]
---
# ② 요구사항 단계 실행

추가 메모: $ARGUMENTS

1. `project/decisions.md`, `project/status.md`, `project/01_market/handoff.md`가 있는지 확인한다. handoff가 없으면 중단하고 /phase-1-research를 안내한다.
2. `requirements-analyst` 호출 → project/02_requirements/srs.md 초안(v0.1)
3. `marketing-strategist` 호출(②단계 검토 모드) → project/02_requirements/marketing-review.md
4. `requirements-analyst` 재호출 → 검토 의견을 반영해 srs.md v0.2를 만들고 반영표를 채운 뒤 handoff.md(② → ③)를 작성한다.
5. SRS 「확인 필요 질문」이 있으면 사용자에게 제시한다. 답변을 받으면 4번을 반복한다(최대 2회).
6. `pm-orchestrator`로 점검하고 status.md를 갱신한다(②: 검토대기).
7. 사용자에게 "G2 게이트 대상입니다. `/gate`를 실행해 승인해 주세요."라고 안내하고 **멈춘다**.
