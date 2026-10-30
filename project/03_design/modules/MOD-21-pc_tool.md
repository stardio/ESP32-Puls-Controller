# 설계→코딩 인터페이스: pc_tool (PC 설정 도구)

| 모듈 ID | 버전 | 작성 | 대상 언어/플랫폼 | 추적 SRS ID |
|---|---|---|---|---|
| MOD-21 | 0.1 | architect | Python 3 (Windows [가정]), GUI tkinter [가정] + CLI | FR-016, FR-019, FR-021, FR-022, FR-018, NFR-015, NFR-020, FR-010 |

## 1. 책임 (이 모듈이 하는 일 / 하지 않는 일)
- 한다: 이더넷 Modbus TCP로 컨트롤러 접속(DC-09). 기능: (1) 장치 정보(모델·축 수·펌웨어·맵 버전), (2) 축 파라미터·시스템 설정 전부 조회·변경·저장(SAVE), (3) 백업(JSON 파일: 모델, fw_version, regmap_version, 전 파라미터 값)과 복원(다른 장치로 복원 포함, 모델·맵 버전 호환 검사, 복원 후 되읽기 비교), (4) 네트워크 설정 변경(IP 등, net_apply), (5) 시운전 패널(서보 ON/OFF, JOG±, 절대/상대 이동, 원점복귀, 정지, 알람 해제, 비상정지 해제), (6) 진단(입력 상태, 정지 원인, 알람 이력, 진단 계수), (7) 펌웨어 갱신(MOD-17 창 사용), (8) 1축 초기 설정 마법사(NFR-015: 펄스 형식 → 최고 속도 → 가감속 → 입력 논리 → 원점 방식 → JOG 확인), (9) 비상정지 보조 정지 고지 문구 상시 표시(NFR-020).
- 하지 않는다: 무선 설정(FR-017 범위 밖), 화면 데이터 제작(MOD-23~25).

## 2. 의존성
- 사용: MOD-20 생성 `regmap.py`, Modbus TCP 클라이언트(pymodbus [가정, 버전·라이선스 ④에서 확인] 또는 자체 최소 클라이언트), tkinter(표준 라이브러리).
- 사용하는 쪽: 사용자, ⑥ 매뉴얼, ⑤ 시험(일부 수동 시험).

## 3. 입출력 정의
| 구분 | 이름 | 타입/단위 | 범위 | 설명 |
|---|---|---|---|---|
| 입력 | 접속 IP·포트·유닛 ID | str, int | 기본 192.168.0.10:502 [가정] | |
| 입력 | 백업 파일 | JSON | | 스키마: {format_ver, model_id, fw_version, regmap_version, created_at, params: {name: value}} |
| 출력 | 백업 파일 | JSON | | |
| 출력 | 복원 결과 | 항목별 일치/불일치 | | 되읽기 비교 |
| 오류 | 접속 실패, Modbus 예외, 호환 불가(모델·맵 버전) | | | 한국어 메시지 |

공개 API(시그니처 수준):
```python
class Controller:
    def __init__(self, host: str, port: int = 502, unit: int = 1): ...
    def info(self) -> "DeviceInfo": ...
    def read_params(self, axis: int | None = None) -> dict[str, int]: ...
    def write_params(self, values: dict[str, int], axis: int) -> list["WriteResult"]: ...
    def save(self) -> None: ...
    def axis_command(self, axis: int, cmd: str, **kw) -> int: ...      # returns cmd_result
    def jog(self, axis: int, direction: int, hold: bool) -> None: ...   # keepalive while held
    def diagnostics(self) -> "Diag": ...
    def alarm_history(self) -> list["AlarmEntry"]: ...
    def firmware_update(self, image_path: str, progress_cb=None) -> None: ...

def backup(ctrl: Controller, path: str) -> None: ...
def restore(ctrl: Controller, path: str, verify: bool = True) -> "RestoreReport": ...
# CLI: pctool backup --host ... out.json / pctool restore --host ... in.json / pctool info
```

## 4. 제약 조건
- JOG 버튼 누르는 동안 주기 요청(≤ `jog_keepalive_ms`/2) 송신, 떼면 JOG_STOP. 창 포커스 상실 시 JOG_STOP [가정].
- 운전 명령은 확인 대화 후 실행 [가정](오조작 방지). 비상정지 해제는 "안전 확인" 문구 표시 후.
- 복원은 축 정지·서보 OFF 상태에서만, 순서: 정지 중 반영 항목 → 나머지 → SAVE → 되읽기 비교.
- 레지스터 주소·형은 `regmap.py`만 사용(하드코딩 금지).
- 화면 문구 한국어.

## 5. 완료 기준 (각 항목은 단위 테스트로 검증 가능해야 함)
| # | 기준 | 검증 방법 |
|---|---|---|
| 1 | 전 파라미터 설정 → 백업 → 다른 장치에 복원 → 값 일치 | 시연(FR-016), 자동화 HIL |
| 2 | 백업/복원 로직 단위 시험(모의 Modbus 서버) | pytest |
| 3 | IP 변경 → 재기동 → 새 IP로 접속 | HIL(FR-022) |
| 4 | JOG 버튼 유지·해제·도구 강제 종료 시 정지 | HIL(FR-019) |
| 5 | 고지 문구 표시 | 검사(NFR-020) |
| 6 | 무교육 사용자 1축 설정 시간 측정 | 사용성 시험(NFR-015, ⑤) |

## 6. 미확인·가정
- OS(Windows), GUI 툴킷(tkinter), Modbus 라이브러리 [가정]. 배포 형식(실행 파일 묶음 여부) 미확인.
- 장치 IP를 모를 때의 탐색 수단(브로드캐스트 탐색 등) 미정(interface-spec 이슈 #4).
