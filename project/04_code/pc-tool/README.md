# svtool: 제품용 PC 도구 (D-074, SRS v0.9 FR-035·FR-038)

Python 3 표준 라이브러리만 사용한다. RTU(COMx)만 선택 의존성 `pyserial`이 필요하다(이 환경에는 미설치라 RTU 경로는 실행 시험하지 못했다).
레지스터 주소는 전부 `svtool/regmap.py` 한 파일에 있다(interface-spec §5, §5.11, §5.12 기준). 펌웨어 오프셋이 바뀌면 이 파일만 고친다.

## 사용법

```
python -m svtool backup --target 192.168.0.220 -o backup.json      # COM3 도 가능(RTU)
python -m svtool restore backup.json --target 192.168.0.220        # 차이 표시 -> y 확인 -> 잠금 해제 -> 쓰기 -> SAVE -> 되읽기 확인
python -m svtool restore backup.json --target IP --dry-run         # 차이만 표시
python -m svtool factory-reset --target IP                         # 'FACTORY RESET' 입력 필요
python -m svtool net-reset --target IP                             # 'NET RESET' 입력 필요
python -m svtool status --target IP                                # wiring_status 해석, 종료코드 0 정상 / 3 이상
python -m svtool alarm --target IP [--history] [--messages 경로]   # 한글 원인·조치, 종료코드 4 = 알람 있음
python -m svtool sim --port 5020 [--axes 2]                        # 시뮬레이터
```
공통 옵션: `--unit`(기본 1), `--baud`, `--parity`, `--timeout`. restore 옵션: `--yes`, `--force`(regmap_version 불일치 무시), `--no-net`(네트워크 설정 제외). reset 옵션: `--confirm 문구`(비대화형), `--reboot`.

## 백업 파일(JSON)
`format`, `fw_version`(+raw), `regmap_version`, `model_id`, `axis_count`, `created`, `registers[]`(주소, 이름, 그룹, 형, 원시 워드, 해석값).
그룹: system(0x0110~0x0114, 0x0130), axis1/axis2(B+0x100~0x12B, B+0x140~0x147), points(0x6000~, 포인트당 7워드), network(0x4000~0x400E), legacy(0x0000~0x0003, 0x0014~0x0017).
복원 규칙: regmap_version이 다르면 중단(--force 예외), 축 수가 장치보다 많으면 중단, 펌웨어 버전이 다르면 경고만. 변경된 항목만 쓰고, 그룹마다 `unlock_key`(0x0102)=0x4D56을 다시 써서 10 s 창을 갱신한다. 네트워크 값은 재부팅 후 적용된다.

## 권고 기본값 (규칙 9, 설계서에 값이 없어 이 도구가 정한 것)
- **[권고] sys_command 번호: FACTORY_RESET=7, NET_RESET=8.** §5.12가 이름만 정하고 번호가 없다. 근거: 1~6 다음 빈 번호. 펌웨어 확정 후 `regmap.py`만 수정.
- [권고] 확인 문구 `FACTORY RESET` / `NET RESET`(영문 대문자, 한글 IME 오입력 방지).
- [권고] RTU 기본 19200 8E1(Modbus 규격 권장 패리티). 장치의 rtu_baud 설정과 맞출 것.
- [권고] 축별 보조 코일 주소는 축 번호 1 기준 0x10(축1), 0x20(축2). §5.5의 "0x0010×n"에서 n=0이면 레거시 코일 0~3과 겹치므로 n을 1부터로 해석.
- [권고] 단위 환산: mm 모드 펄스 = µm값 × pulses_per_rev × gear_num / (gear_den × lead_um), ° 모드는 lead 대신 360000(m°), 반올림은 0에서 먼 쪽. 펄스 모드의 단위값은 milli-pulse(×0.001)로 해석.
- [권고] 모터 돌려보기 속도 pps = test_rpm × pulses_per_rev / 60(모터 축 기준, 감속비 무관).
- 알람 문구표는 `project/03_design/hmi/alarm-messages.md`(없는 지금)가 생기면 `| 0x코드 | 이름 | 원인 | 조치 |` 표 행을 읽어 덮어쓴다. 없으면 `alarms.py`의 초안 표(§5.8 코드)를 쓴다.
- status 판정: wiring_status b15=0이면 "점검 무효". 축별 b0 STOP, b1 START 대기, b2 루프백이 모두 1이면 정상. b3(서보 ON 출력)은 참고만 표시. 이더넷 링크 b8, RTU 수신 b9(실제 모드가 RTU일 때만 판정).

## 시뮬레이터 (FR-038)
`sim`은 같은 레지스터 맵의 Modbus TCP 서버다(FC 01/02/03/04/05/06/15/16, 예외 02/03 규칙, 32비트 반쪽 쓰기 03, 네트워크 워드 잠금, 잠금 명령 0x11). 시간 기반 동작: 위치 증가·완료(done 비트, 정지 원인), STOP 감속, QUICK_STOP, JOG, 원점복귀(1 s), 돌려보기(500 ms 유지), 단위 변환 이동(13/14), 포인트 티칭·이동(17/18), 레거시 창 1축 호환(코일 0~3, HR 0~0x17), 알람 이력, 소프트 리밋(0x14), 설정 변경 후 미저장 비트(재부팅하면 저장 안 한 값 소실).
HMI 패널은 PC의 IP와 `--port`(패널이 502만 되면 `--port 502`, 관리자 권한 필요)로 접속한다.
시험용 콘솔(터미널에서 실행 시 stdin): `stop on|off`(STOP 입력), `alarm <축 0/1/2> <코드>`, `clear`, `limit <축> +|- on|off`, `wiring x.stop|x.start|x.loop|y.*|eth|rtu on|off|auto`, `link on|off`, `status`, `quit`.
단순화: 등속 이동(S자 곡선 없음), 전원 켠 직후 위치는 유효, 비상정지 후 위치 무효(estop_pos_policy=0), JOG 유지 신호 끊김 정지 미구현, ESTOP_RELEASE(sys_command 1)는 D-072에 따라 0x10 거부, 통신두절 정지 미구현.

## 시험 결과 (2026-11-01, 이 환경: Python 3.x, pytest 9.2.0)
```
cd project/04_code/pc-tool && python3 -m pytest -q tests
31 passed in 13.95s
```
내용: 백업 파일 내용·버전, backup → 값 변경 → restore 왕복 일치 + SAVE, dry-run, regmap 불일치 중단, 펌웨어 차이 경고, 잠금 없이 네트워크 쓰기 거부(예외 03)·10 s 만료·잘못된 키, 잠금 명령 0x11, 32비트 반쪽 쓰기·읽기 전용·미정의 주소·범위 밖, 공장/네트워크 초기화(확인 문구 불일치 시 취소), status 정상/이상(STOP·루프백), wiring 비트 해석, 알람 출력(+이력)·문구표 파싱, 단위 변환(mm/°/기어/rpm/max_rpm), 단위 이동·과속 0x17, 돌려보기, 포인트 티칭·이동, 이동·STOP·비상정지 래치 해제(D-072)·알람 차단·소프트 리밋·레거시 코일 시작·재부팅 후 미저장 소실.
수동 확인: `sim --no-console` 프로세스에 `status`, `backup`, `alarm`, `factory-reset`(표준입력으로 문구) 실행 성공.
**미시험:** RTU 경로(pyserial 없음, 실장비 없음), 실제 펌웨어 접속, `alarm-messages.md` 실파일 파싱(파일 없음, 형식은 가정).

## 이슈 제기 (설계/스펙)
1. sys_command FACTORY_RESET·NET_RESET 번호 미정(위 권고 7·8). LOAD_DEFAULT(3)와의 차이(네트워크 포함 여부)도 정의 필요. 시뮬레이터는 3과 7을 동일하게 처리.
2. §5.5 "코일 0x0010×n" 축 번호 기준 모호(위 권고).
3. 포인트 테이블 0x6000~가 §5.2의 단독 운전 순서(0x6000~0x6FFF)와 주소가 겹친다.
4. §5.3 ESTOP_RELEASE(sys_command 1)와 §5.11 D-072("해제 명령 없음")가 상충. 도구는 해제 명령을 쓰지 않는다.
5. pulse_res_hz(+0x12A)는 값 범위 미확인이라 백업에 포함하되 복원 시 장치가 거부할 수 있다(복원 오류로 보고됨).
6. 장치별 값(IP, unit_id)이 백업에 들어가므로 다른 장치로 복원할 때는 `--no-net` 권장.
