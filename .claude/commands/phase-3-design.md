---
description: ③ 아키텍처·인터페이스·BOM 설계 (G2 승인 필수)
argument-hint: [설계 방향 메모(선택)]
---
# ③ 설계 단계 실행

설계 메모: $ARGUMENTS

0. **게이트 확인:** `project/gates/G2-requirements.md`를 읽는다. 파일이 없거나 결정이 `승인`/`조건부 승인`이 아니면
   "G2 미승인으로 ③단계를 시작할 수 없습니다"라고 보고하고 **즉시 중단**한다. 조건부 승인이면 그 조건을 architect에게 전달한다.
1. decisions.md와 02_requirements/handoff.md를 읽고, status.md에서 ③을 "진행중"으로 바꾼다.
2. `architect` 호출(1차) → architecture.md (블록 구조와 후보 대안)
3. `parts-selector` 호출 → parts-comparison.md, bom-draft.md (architecture.md 블록 기준)
4. `architect` 재호출(2차) → BOM을 반영해 interface-spec.md, modules/*.md, handoff.md(③ → ④, 추적표 포함)를 작성한다.
5. `pm-orchestrator`로 점검한다. 모든 '필수' SRS ID가 추적표에 있는지 확인한다.
6. "G3 게이트(아키텍처·핵심 부품 선정) 대상입니다. `/gate`를 실행해 주세요."라고 안내하고 **멈춘다**.
