---
description: ⑥ 매뉴얼·설치 가이드·데이터시트 작성 후 G6(양산/출시·가격) 승인 요청 준비
argument-hint: [문서 범위 메모(선택)]
---
# ⑥ 문서 단계 실행

메모: $ARGUMENTS

1. `project/05_test/handoff.md`가 있는지 확인한다(없으면 중단). status.md에서 ⑥을 "진행중"으로 바꾼다.
2. `tech-writer` 호출 → 06_docs/datasheet.md, installation-guide.md, user-manual.md, handoff.md
3. `marketing-strategist` 호출(가격안 모드): 가격 전략 **제안**을 07_marketing/pricing-proposal.md에 작성한다. G6 검토 자료로 쓴다.
4. `pm-orchestrator`로 점검한다: 미해결 Critical 결함, 미검증 사양 수, 원가 대비 가격 차이.
5. "G6 게이트(양산/출시 결정과 가격 책정) 대상입니다. `/gate`를 실행해 주세요."라고 안내하고 **멈춘다**.
