# firmware 재리뷰 2차 (경량, 2026-10-31)

범위: firmware-review-1 Major 7건(M-01~M-06, T-01)의 수정분만. 근거는 notes/firmware.md §11과 아래 파일:줄이다.
시험 결과는 메인 세션 보고(make run 0 failures, pytest 32 passed)를 인용한다. 검토자가 직접 실행하지는 않았다.

## 판정: **통과**

Major 7건은 모두 해소됐다. M-03은 호스트 시험이 없으므로 HIL T-06의 "저장 직전 START"로 확인한다. 새로 발견한 것은 Minor 2건이다.

## Major 해소 확인

| ID | 결과 | 근거 |
|---|---|---|
| M-01 | 해소 | `fw_hal_stop_release()`(hal_io.c:130-139)가 ISR과 같은 `s_stop_mux`(hal_io.c:105-107) 안에서 입력과 stable_ms를 확인한 뒤 래치를 지운다. 해제 판정(fw_axis.c:111)은 다른 모든 거부 조건(92-107) 뒤에 있다. 입력 활성이나 io 미초기화는 95행에서 바로 0x10으로 거부한다. ESTOP_RELEASE 거부는 유지했다(fw_motion.c:81-82) |
| M-02 | 해소 | poll_once의 레벨 백업(hal_io.c:150-153, fw_stop.h:42-47)이 들어갔다. 래치 중 펄스가 나가면 끊는다(hal_io.c:159). `fw_axis_poll`도 `latched \|\| input`을 본다(fw_axis.c:237) |
| M-03 | 해소(HIL 남음) | 저장·재부팅 결정과 `s_saving`/`s_reboot_commit` 설정이 같은 락 구간에 있다(fw_motion.c:203-217). 시작 경로 3개가 모두 `start_blocked()`를 확인한다(60, 69, 196). 이동 시작 명령은 START_ABS/REL(fw_axis.c:201-202)과 레거시(160)뿐이고 모두 막힌다 |
| M-04 | 해소 | `fw_cfg_defaults_model()`(fw_params.c:169-172)을 첫 부팅·손상 시(fw_regmap.c:87 → fw_motion.c:248, 272)와 LOAD_DEFAULT(fw_motion.c:95)에서 쓴다 |
| M-05 | 해소 | 잠금 없이 네트워크 값을 바꾸는 쓰기는 예외 03으로 거부한다(fw_regmap.c:344). 네트워크만 바뀐 경우는 `cfg_changed`를 부르지 않는다(349-354) |
| M-06 | 해소 | boot_status b6/b7(fw_motion.c:301-304)을 둔다. fault 경로가 묶이지 않은 축은 `engine_allowed`가 false(fw_axis.c:21-27)이고 알람은 0x0602(14)다. recover 실패 시 래치를 다시 세운다(hal_io.c:137, 158). 정체 감시는 미구현이며 notes §11에 T-07 이후로 미뤄 두었다(수용) |
| T-01 | 해소 | notes §11에 시험 목록이 있다. 0 failures는 메인 세션 보고를 인용 |

## D-072 해제 경로: STOP 활성 중 해제 불가 확인

- 해제는 `fw_es_release()`(fw_stop.c:18-23) 한 곳뿐이다. HMI 경로(hal_io.c:133-135)와 물리 START 경로(hal_io.c:154-157) 모두 `s_stop_mux` 안에서 호출한다. 입력 읽기와 `latched=0` 기록 사이에 ISR이 끼어들 수 없다. 다른 코어의 ISR은 스핀락에서 기다리고, 같은 코어의 ISR은 마스크된 뒤 해제 직후 실행되어 다시 래치한다.
- ISR 트립은 `stable_ms=0`도 함께 기록한다(fw_stop.h:32). 그래서 트립 직후 10 ms 동안은 해제되지 않는다.
- 남은 창: mux를 나온 뒤부터 `release_hw()` 전까지 ISR이 다시 래치하는 경우(hal_io.c:135-137). 입력이 활성이면 IDF recover가 실패해 relatch로 간다. 입력이 이미 풀렸으면 recover가 성공해 0을 반환하지만, 래치가 남아 있어 `fw_hal_pulse_start` 재확인(hal_pulse.c:205, 226)에서 0x10으로 거부된다. 따라서 STOP 중 펄스는 나가지 않는다. 다만 recover 실패 판정은 IDF 동작에 의존하므로 HIL에서 확인한다(notes §11 M-01 HIL 항목과 같다).

## 새 지적 (Minor)

| ID | 심각도 | 위치 | 근거 | 권고 |
|---|---|---|---|---|
| n-01 | Minor | fw_regmap.c:349-354, fw_motion.c:162·214 | 네트워크만 바꾼 값은 RAM에만 있고 `s_save.dirty`가 서지 않아 FW_SSB_UNSAVED에 보이지 않는다. 이후 아무 일반 파라미터 변경이 자동 저장되면 NET_APPLY 없이 네트워크 값도 함께 NVS에 저장된다(쓰기 시점에 잠금은 확인했으므로 위험은 제한적) | 네트워크 미저장 플래그를 따로 두어 UNSAVED에 표시한다. 자동 저장 시 `net`은 저장된 값으로 유지할지, 아니면 이 동작을 §12에 명시할지 정한다 |
| n-02 | Minor | fw_motion.c:196-199 | 저장 중 받은 물리 START를 보류했다가 저장이 끝난 뒤 실행한다(m-04). 실제 기동이 NVS 쓰기 시간만큼 늦어진다. STOP 재래치 시 거부되므로 안전 문제는 아니다 | T-06에서 보류 지연을 측정하고 notes에 동작을 적는다 |

## 이슈 제기
- 없음(review-1 이슈 1~3은 메인 세션 문서 갱신 대상으로 유지).
