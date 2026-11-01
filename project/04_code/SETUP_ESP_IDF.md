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
