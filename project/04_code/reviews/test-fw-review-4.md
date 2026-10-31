# test-fw 리뷰 4차 (경량, 커밋 46aa408 변경분)

- 범위: 기준선 v0.4 반영분(축별 STOP, START 엣지, GPIO33 공용 OE, TFW_AX2_IO_PRESENT, 서보 ON DO1/DO2). 이전 지적 재검토는 하지 않았다.
- 근거: design-baseline-v0.4.md §2·§3·§7, decisions.md D-058·D-060·D-065·D-068, notes/test-fw.md §4·§4a·§7.
- 테스트는 실행하지 않았다. notes §4a의 결과(`checks=16629 failed=0`, `274 passed`)만 인용한다.

## 판정: **조건부 통과**

펌웨어의 STOP 경로(ISR, MCPWM 폴트, 래치, START 엣지)는 축별로 독립이고 GPIO33을 건드리지 않는다. Critical은 없다. Major 1건(V4-01)은 T-07 시험 운용 문제로, 축 독립성을 실측하기 전에 해소해야 한다.

## 중점 확인 결과

| # | 항목 | 결과 | 근거 |
|---|---|---|---|
| 1 | 축별 STOP 독립성 | 펌웨어 경로는 적합. 시퀀서는 아님(V4-01) | ISR이 자기 축만 래치·정지한다(`tfw_estop_io.c:59-71`, 다른 축은 PCNT 읽기만 함). 폴트는 그룹 g = 축 g(`:161-182`)이고 정리 정지도 자기 축만 한다(`:107`). OE는 `oe_set`에서만 쓰고(`tfw_pulse2.c:181-184,261,311`) `tfw_pulse2_stop`에는 OE 코드가 없다(`:408-417`) |
| 2 | STOP 중 START 무시, 해제 후 엣지에서만 재기동 | 적합 | STOP 중에 생긴 엣지는 `stable`을 갱신하면서 소비된다(`tfw_core.c:117-121`). 그래서 START를 누른 채 STOP을 풀면 새 엣지가 없다. 부팅 때 이미 눌린 START는 엣지가 아니다(`tfw_estop_io.c:227-228`). 해제 후 10 ms 안정 조건이 충족되지 않은 엣지는 거부·계수된다(`:94`, `tfw_core.c:99`) |
| 3 | AX2_IO_PRESENT=n이면 축2 입력을 읽지 않으면서 펄스는 생성 | 적합 | STOP 핀 -1(`tfw_main.c:326`), START 핀 -1(`tfw_estop_io.c:151`). 폴링·ISR·폴트 바인딩·gpio_config를 모두 건너뛴다(`:102,162,194,218`). `blocking_axis(1)`은 false다(`:77`). 단 `s_init_ok`가 먼저 검사된다(`:76`). 축2 생성은 IO 옵션과 무관하다(`tfw_pulse2.c:279-293`) |
| 4 | 극성 | 적합(풀 저항 V4-02) | Kconfig: ESTOP_ACTIVE_LOW 기본 n, START_ACTIVE_LOW 기본 y, PULSE_OE_ACTIVE_LOW 기본 n(`Kconfig.projbuild:15-17,47-58`). STOP은 POSEDGE이고 폴트 active_level은 1이다(`tfw_estop_io.c:167,199`). bool 매크로는 `tfw_cfg.h` 보조 매크로로 사용한다(N-01 유지) |
| 5 | ISR·IRAM 회귀 | 회귀 없음 | ISR에서는 esp_timer를 쓰지 않고 사이클 카운터만 쓴다(`:35,66`, M-01). 래치는 정지보다 먼저 건다(N-03, `:64-71`). IRAM 플래그는 IRAM 빌드에만 붙는다(`:189-191`). ISR에서 호출하는 `tfw_estop_input_active`는 없다(이 함수는 flash에 있는 `gpio_get_level`을 호출한다) |
| 6 | 권고 기본값 4건(notes §4a 182행) | 타당. 단서 있음 | (1) 창 freq/count로 `run_fixed`: 시험 펌웨어로서 타당하다. count=0이면 STOP이나 정지 명령이 올 때까지 무한 운전한다(`tfw_pulse2.c:403`). 이 점을 notes에 적어야 한다. (2) START 엣지가 해제와 시작을 함께 하는 것은 D-058과 맞다. Modbus 해제(0x7061)와 래치 중 RUN 거부는 기준선 §3 "래치·리셋 명령 없음"과 다르므로 V4-04로 기록한다. (3) es_state 최댓값, trip_count 합, boot_level 비트: 레지스터 맵을 유지하므로 타당하다. 축별 트립 구분은 LA로 한다. (4) START 활성 LOW [가정]: 부팅 로그로 확인하는 절차가 있어(notes §4-0) 타당하다 |

## 지적

| ID | 심각도 | 파일:줄 | 근거 | 권고 |
|---|---|---|---|---|
| V4-01 | Major | `tfw_main.c:121`, `tfw_seq.c:60`, `host/scenario_example.yaml:26` | T-07은 `RUN_FIXED axis_mask: 3`으로 시퀀서를 쓴다. `hw_aborted`는 `mask_blocked(axis_mask)`이므로 축1이 STOP되면 시퀀스가 끝나고, `started[]`인 축2도 `hw->stop`으로 정지한다. 그 결과 notes §4-7의 기대 결과 "축1만 멈추고 축2는 계속"과 기준선 §3 "다른 축은 영향받지 않는다"를 T-07 실측에서 볼 수 없다. 운용자는 축2가 수 ms 늦게 멈추는 것을 보게 된다. notes §7 첫 항목이 이 동작을 인정하지만, T-07 시나리오는 그대로다. judge도 축2 지속 여부를 판정하지 않는다(`judge.py:217-235`) | (a) 시퀀서의 중단을 축별로 바꾼다. STOP된 축만 중단하고 나머지 축은 끝까지 운전한다. 또는 (b) T-07을 축1 시퀀스와 축2 독립 `run_fixed`로 나누고, LA에서 축2 펄스가 계속되는지와 GPIO33이 HIGH로 유지되는지를 확인 항목으로 시나리오와 notes에 넣는다 |
| V4-02 | Minor | `tfw_estop_io.c:167,197-198` | NC(활성 HIGH)에서 내부 풀다운을 켠다. 캐리어와 MCU 사이 선이 끊기면 GPIO는 "정지 아님"으로 읽힌다. 기준선 §3의 NC 취지(단선 시 정지)는 현장 측 단선에만 성립하고, 그것도 외부 풀업이 있을 때뿐이다. 외부 풀업이 있는지는 미확인이다 | 활성 HIGH일 때는 내부 풀업 또는 풀 없음을 권고 기본값으로 둔다. 축2는 IO가 없으면 이미 판독하지 않으므로(D-068) 영향이 없다. 캐리어 풀 저항 값은 보드 확인 항목으로 남긴다 |
| V4-03 | Minor | `tfw_estop_io.c:94` 대 `tfw_main.c:185-187`, `:127` | (a) START 엣지 해제(`tfw_estop_release_axis`, es_task, 락 없음)와 Modbus 해제(`on_effect`, `s_cmd_lock`)가 같은 `s_es[ax]`와 `recover_from_fault`를 동시에 호출할 수 있다. (b) `on_start_edge`가 `portMAX_DELAY`로 락을 기다리는 동안 es_task(1 ms 폴링)가 멈춘다. 예를 들어 `cfg_save`의 NVS 쓰기 중이면 경로 B 단독일 때의 정리 정지(`:105-108`)가 늦어진다. 하드웨어 정지와 ISR 정지는 영향을 받지 않는다 | 해제 호출도 `s_cmd_lock` 안으로 옮긴다(콜백 쪽에서 해제와 시작을 함께 처리). 콜백은 짧은 타임아웃을 쓰거나 플래그를 넘겨 다른 태스크에서 시작한다 |
| V4-04 | Minor | notes §5 | 기준선 §3·D-058은 "래치·리셋 명령 없음"인데, 시험 펌웨어는 Modbus 해제(0x7061)와 래치 중 RUN 거부(0x10)를 유지한다. T-07 래치 시험용이라 타당하지만 §5 "설계와 다른 점"에 없다 | §5에 한 줄을 추가한다(제품 펌웨어에는 넣지 않는다는 점 포함) |
| V4-05 | Minor | `tfw_main.c:327-328` | `tfw_estop_init`이 es_task를 만든 다음에 `set_start_cb`를 한다. 첫 수 ms 동안 들어온 START 엣지는 계수만 되고 시작되지 않는다. 영향은 미미하다 | 콜백 등록을 init 앞으로 옮긴다 |

## 판정 조건

1. V4-01 (a) 또는 (b)를 반영한 뒤 T-07 축 독립성을 실측한다.
2. V4-02~05는 다음 수정 때 함께 처리해도 된다. 판정을 막지 않는다.
