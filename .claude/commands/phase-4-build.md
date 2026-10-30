---
description: ④ 모듈별 구현 — coder → code-reviewer 반복 (G3 승인 필수)
argument-hint: [모듈명(생략 시 전체, 의존성 순)]
---
# ④ 구현 단계 실행

대상 모듈: $ARGUMENTS

0. **게이트 확인:** `project/gates/G3-design.md`의 결정이 `승인`/`조건부 승인`이 아니면 **즉시 중단**한다.
1. 03_design/handoff.md의 모듈 목록과 의존성으로 구현 순서를 정한다. status.md에서 ④를 "진행중"으로 바꾼다.
2. 모듈마다 순서대로 다음을 반복한다.
   a. `coder` 호출: 모듈 설계서 경로 1개만 전달한다 → src/, tests/, notes/
   b. `code-reviewer` 호출: 같은 모듈의 경로를 전달한다 → reviews/<모듈>-review-<n>.md
   c. 판정이 '재작업'이거나 Critical/Major 지적이 있으면 리뷰 경로를 coder에게 전달해 a로 돌아간다. **최대 3회**까지 반복한다.
   d. 3회 후에도 통과하지 못하면 해당 모듈을 '보류'로 하고 status.md에 이슈를 남긴 뒤 다음 모듈로 넘어간다.
3. 메인 세션에서 04_code/handoff.md를 작성한다: 모듈별 상태, 테스트 결과, 미해결 지적, 설계와 다른 점.
4. `pm-orchestrator`로 status.md를 갱신하고 결과를 보고한다. 다음 명령은 `/phase-5-verify`이다.
