# firmware 리뷰 3차 (경량) — FR-029~FR-035 초보자 기능 추가분

- 검토일: 2026-11-01 / 검토자: code-reviewer
- 기준: interface-spec §5.12(구현 확정 사항 포함), decisions.md D-058·D-072·D-076, notes/firmware.md §13
- 범위: fw_unit.c, fw_axis.c(13~18 명령), fw_regmap.c(wiring_status·alarm_hint·포인트·reset), fw_params.c(net_reset·nrhold·이미지 v2), fw_motion.c(부팅 보유 감지), pc-tool/svtool/regmap.py(주소·번호 대조만)
- 시험: 직접 실행하지 않았다. 메인 세션 전달값을 인용한다. firmware `make run` 4종 0 failures(test_beginner 148,840 checks), pytest 32 passed, pc-tool pytest 31 passed. notes §13.4에는 148,838로 적혀 있어 2건 차이가 있다(문서 갱신만 필요).

## 판정: **조건부 통과** (Major 2건 수정 후 통과)

## 지적 항목

| ID | 심각도 | 파일:줄 | 근거 | 수정안 |
|---|---|---|---|---|
| R3-M1 | Major | firmware/components/fw_motion/fw_motion.c:62 | `start_blocked()`(저장 중·재부팅 확정 후 시작 금지, 리뷰 M-03)가 `FW_CMD_START_ABS/REL`에만 걸린다. 새 시작 명령 13·14(UNIT), 15·16(TEST), 17(POINT_GO)은 막히지 않는다. 그래서 NVS 기록 중에 펄스가 시작될 수 있다. 또 `s_reboot_commit`가 선 뒤(230~238행, 락 밖)에 TEST가 시작되면 `fw_hal_reboot()`가 감속 없이 출력을 끊는다. | 차단 조건을 "시작 계열 명령 전체"(1, 2, 13~17)로 넓힌다. 예: `fw_cmd_is_start(cmd)` 헬퍼를 fw_core에 두고 호스트 시험에서 13~17 거부(0x13)를 확인한다. TEST keepalive(이미 운전 중)는 허용해도 된다. |
| R3-M2 | Major | firmware/components/fw_core/fw_params.c:197-209, fw_motion.c:222-227·327 | 발동 조건은 "부팅 첫 샘플부터 `di_raw` b0(X START 활성)와 b1(X STOP 활성)이 끊김 없이 10 s"뿐이다. NC STOP이 단선됐거나 아예 배선되지 않은 경우(초보자 벤치에서 흔하다) b1은 항상 1이다. 이때 START만 10 s 눌려 있어도 발동한다. START가 점퍼로 묶였거나, PLC 출력이 START를 레벨로 쥐고 있거나, 버튼이 고착된 경우도 마찬가지다. 그러면 사용자 IP가 사라진다. 또 원인이 그대로라 재부팅할 때마다 10 s 뒤 다시 발동해 **약 10 s 주기 재부팅 루프**가 된다(net이 이미 공장값이어도 `s_reboot_req`는 선다). 사람이 의도한 조작인지 구별할 수단이 없다. | (1) 확인 동작을 하나 더 요구한다. 10 s 보유 뒤 **STOP을 해제(NC 닫힘)** 하고 5 s 안에 해야 발동하게 한다. 단선된 STOP은 닫힘 엣지를 만들 수 없으므로 오발동이 막힌다. (2) net 워드가 이미 공장값이면 저장·재부팅을 하지 않는다(루프 차단). (3) `boot_status & FW_BOOT_STOP_IO`가 0이면 감지하지 않는다(R3-m3). D-076 동작이 바뀌므로 메인 세션이 decisions에 권고 기본값으로 기록한다. boot_hold 시험에 "STOP 상시 활성 + START 상시 활성" 사례를 추가한다. |
| R3-m1 | Minor | firmware/components/fw_motion/fw_axis.c:217, 336-337 | TEST의 감속에는 운전 조작 `decel_time_ms`(op, 10~60000 ms)를 그대로 쓴다. 홀드형(데드맨) 기능인데 사용자가 감속 시간을 길게 두면, 손을 뗀 뒤에도 그만큼 계속 움직인다. spec §5.12는 감속 시간을 정하지 않았다. | TEST 전용 감속을 상한으로 묶는다. 예: `min(op decel, def_decel)` 또는 고정 상한 [권고] 200 ms. notes §13.3에 적는다. |
| R3-m2 | Minor | firmware/components/fw_core/include/fw_params.h:55, fw_motion.c:284-292 | v1 이미지는 크기·버전 불일치로 거부된다. 이때 E-PARAM이 걸리고 기본값(IP 192.168.0.220 포함)으로 올라오며, SAVE 전까지 운전이 금지된다. 안전 쪽 동작이라 문제는 아니다. 다만 notes §13.3-1은 "기본값으로 시작"이라고만 적어 운전 금지와 IP 변경이 빠져 있다. 벤치 장치를 가진 사용자는 업데이트 뒤 접속이 끊길 수 있다. | notes와 업데이트 안내에 "v2 첫 부팅: E-PARAM, IP 공장값, 재설정 후 SAVE"를 적는다. 이전 이미지 이관(net·par 복사)은 필요할 때만 한다. |
| R3-m3 | Minor | firmware/components/fw_motion/fw_motion.c:222·327 | I/O 초기화가 실패해도(`FW_BOOT_STOP_IO`=0) `di_raw`를 그대로 보유 감지에 쓴다. wiring_status(fw_regmap.c:110)는 같은 경우를 무효로 처리한다. | 감지 조건에 `FW_BOOT_STOP_IO`를 넣는다(R3-M2와 함께). |
| R3-m4 | Minor | pc-tool/svtool/regmap.py:22 | 주석에 "7/8 are [REC]: spec gives no number"라고 남아 있다. 이 번호는 spec §5.12 구현 확정 표에서 이미 확정됐다. 값(7, 8)은 펌웨어(fw_regmap.h:32)와 일치한다. | 주석을 "spec §5.12 구현 확정"으로 고친다. |

## 확인한 것(문제 없음)

- **TEST 홀드형**(fw_axis.c:206-220, 336-337)
  - keepalive를 500 ms 안에 다시 쓰지 않으면 감속 정지한다. 통신이 끊긴 경우에도 같은 경로로 멈춘다.
  - 감속 중에 같은 명령을 쓰면 0x13, 반대 방향도 0x13이다.
  - STOP 입력이 활성이면 keepalive를 포함해 0x10으로 거부한다.
  - 속도 상한은 `fw_test_hz`(min(1 MHz, max_speed))와 `speed_ok`를 둘 다 거친다.
  - 래치된 STOP을 입력 해제 뒤 TEST로 풀 수 있다. 이는 D-072("명시적 시작 명령") 범위 안이다.
- **단위 명령 0x17**(fw_axis.c:167-176): 변환 오버플로, 0 속도, 100 Hz 미만, start_speed 미만, max_speed·1 MHz 초과를 모두 거부한다.
- **POINT_GO**(fw_axis.c:178-187)
  - `cmd_start_with(absolute)`를 거치므로 위치 무효 조건이 START_ABS와 같다(REQUIRE_HOME).
  - STOP 검사는 `start_move`에서 한다. 우회 경로는 없다(저장 중 차단만 R3-M1).
- **fw_unit.c**
  - 64×64→128 곱셈의 자리올림이 맞다(mid ≤ 3·(2^32−1)).
  - 반올림 자리올림과 `hi ≥ b` 검사로 2^64 초과를 판정한다.
  - 장제법의 carry가 b > 2^63인 경우도 처리한다.
  - 음수는 크기로 계산한 뒤 부호를 붙이므로 반올림이 대칭이다. S32 경계는 −2^31까지 허용한다.
  - 중간값은 최대 2^31 × 1e10이라 128비트 안에 들어간다.
- **wiring_status / alarm_hint**: spec §5.12 확정 표와 비트·우선순위가 같다. 0x0603은 0x0602와 같은 순위다.
- **FACTORY_RESET / NET_RESET**(fw_regmap.c:127-136): 잠금 0x11, 운전 중 0x12, 잠금 해제 1회 소모를 확인했다.
- **PC 도구 대조**(regmap.py): 일치한다.
  - 0x0033 / 0x0034 / 0x0130
  - axis +0x030 / 0x032, +0x04A / 0x04C / 0x04E, par +0x140~0x147
  - axis_command 13~18, sys_command 7·8
  - 포인트 기준 0x6000 + (n−1)·0x80 + i·0x10, 워드 +0/+2/+4/+5/+6
