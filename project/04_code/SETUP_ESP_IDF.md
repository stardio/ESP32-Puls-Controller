# 내 PC에 ESP-IDF 환경 만들기 (ESP32-P4, ESP-IDF v5.5.3)

작성: 메인 세션, 2026-11-01. 버전은 servo1axis 기준선과 같은 v5.5.3(D-024).
권고 기본값(규칙 9): 설치 경로 `C:\esp\esp-idf-v5.5.3`(Windows) / `~/esp/esp-idf-v5.5.3`(Linux·macOS), 대상 칩 esp32p4만 설치.
이 세션은 클라우드에서 동작하므로 PC에서 직접 실행해야 한다.

## Windows 10/11 (권장 순서)
1. **사전 설치**
   - Git for Windows: https://git-scm.com/download/win
   - Python 3.9 이상: https://www.python.org/downloads/ (설치 첫 화면에서 "Add python.exe to PATH" 체크)
2. **저장소 받기**(아직 없으면)
   ```
   git clone https://github.com/stardio/ESP32-Puls-Controller.git C:\work\ESP32-Puls-Controller
   ```
3. **ESP-IDF 설치**(PowerShell, 10~30분, 약 3~5 GB)
   ```
   powershell -ExecutionPolicy Bypass -File C:\work\ESP32-Puls-Controller\project\04_code\tools\setup_idf_windows.ps1
   ```
4. **새 PowerShell 창마다 환경 활성화**
   ```
   C:\esp\esp-idf-v5.5.3\export.ps1
   idf.py --version
   ```
5. **빌드 점검**(6벌: 시험 펌웨어 IRAM on/off, 제품 1축·2축 × TCP·RTU)
   ```
   cd C:\work\ESP32-Puls-Controller
   powershell -ExecutionPolicy Bypass -File project\04_code\tools\build_all.ps1
   ```

주의
- 경로에 **한글·공백이 없게** 한다(툴체인 경로 오류 예방). 그래서 `C:\esp`, `C:\work`를 권장한다.
- 백신이 설치를 느리게 하거나 막으면 `C:\esp`를 검사 예외에 넣는다.
- 공식 GUI 설치 도구(ESP-IDF Installation Manager)를 써도 된다. 이 경우 버전 v5.5.3, 대상 esp32p4를 고른다.

## Linux(Ubuntu) / macOS
```
git clone https://github.com/stardio/ESP32-Puls-Controller.git ~/work/ESP32-Puls-Controller
bash ~/work/ESP32-Puls-Controller/project/04_code/tools/setup_idf_unix.sh
. ~/esp/esp-idf-v5.5.3/export.sh
cd ~/work/ESP32-Puls-Controller && bash project/04_code/tools/build_all.sh
```
Linux는 설치 후 한 번 로그아웃·로그인해야 시리얼 포트(dialout 권한)가 열린다.

## 보드 연결 확인
- P4-ETH 보드를 USB로 연결하고 포트를 확인한다. Windows: 장치 관리자 → 포트(COM & LPT)의 `COMx`. Linux: `/dev/ttyACM0` 또는 `/dev/ttyUSB0`.
- 포트가 안 보이면 케이블이 충전 전용인지 먼저 확인한다(데이터 케이블 필요). USB 드라이버 필요 여부는 보드 USB 칩에 따라 다르다(미확인, 보통 Windows 10/11은 자동 인식).
- 플래시·모니터: `idf.py -B build_on -p COM5 flash monitor` (종료는 Ctrl+]).

## 결과를 알려 줄 것
- `build_all` 출력의 OK/FAIL 6줄
- FAIL이 있으면 `project/04_code/tools/build_all.log` 파일 전체(또는 push). 오류를 보고 코드를 고친다.
- 빌드가 모두 OK면 `test-fw/RUN_ON_PC.md` 순서로 보드 실측을 진행한다.

## 클라우드 개발 환경을 로컬 PC로 옮기기 (2026-11-03)

### 무엇이 어디에 있나
| 항목 | 위치 | 옮기는 방법 |
|---|---|---|
| 문서·결정·현황(decisions, status, SRS, 설계, 마케팅) | 저장소 `project/` | `git clone` |
| 에이전트 11개·슬래시 명령 9개·공통 규칙 | 저장소 `.claude/`, `CLAUDE.md`, `templates/` | `git clone` (로컬 Claude Code가 자동 인식) |
| 펌웨어·시험 펌웨어·PC 도구·시험 코드 | 저장소 `project/04_code/` | `git clone` |
| 회로도 PDF, 구매 단가 발췌 | 저장소 `project/03_design/reference/` | `git clone` |
| 대화 기록 | 클라우드 세션 | 옮기지 않아도 됨. 필요한 내용은 decisions·status에 모두 기록됨 |
| 미커밋 원본(거래명세서·드라이브 매뉴얼 PDF) | 사용자 보관본 | 의도적으로 저장소 밖에 둠 |

### 순서 (Windows 기준)
1. 저장소 받기: `git clone https://github.com/stardio/ESP32-Puls-Controller.git C:\work\ESP32-Puls-Controller`
2. ESP-IDF 설치·빌드 점검: 위 「Windows 10/11」 절
3. Python 도구: `python -m pip install -r project\04_code\requirements-dev.txt`
4. 호스트 C 시험(gcc·make 필요, 선택): WSL2 Ubuntu에서 `sudo apt install build-essential python3-pytest python3-yaml` 후
   `make -C project/04_code/tests/firmware` / `make -C project/04_code/tests/test-fw`
5. Claude Code 설치 후 저장소 폴더에서 `claude` 실행. 첫 요청 예: "status.md와 decisions.md를 읽고 현재 상태를 이어받아 SETUP_ESP_IDF.md 빌드 점검부터 진행해줘"

### 로컬로 옮기면 달라지는 점
- 네트워크 차단이 없어 제조사 매뉴얼(LS·미쓰비시) 원문을 직접 조회할 수 있다(I-012·I-045 해소 가능).
- 보드 플래시·시리얼 로그·빌드 오류 수정을 Claude가 직접 할 수 있다.
- 명령 실행 권한은 로컬 PC 계정 권한이다. 승인 프롬프트를 확인하고 허용한다.
- 클라우드 세션과 로컬 세션을 동시에 쓰면 같은 파일을 따로 고칠 수 있다. 한쪽에서 작업 전 `git pull`, 끝나면 commit·push를 지킨다.
