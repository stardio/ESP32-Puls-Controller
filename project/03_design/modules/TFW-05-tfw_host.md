# 설계→코딩 인터페이스: tfw_host (G3 전 시험 — PC 측 부하·측정·보고 도구)

| 모듈 ID | 버전 | 작성 | 대상 언어/플랫폼 | 추적 SRS ID |
|---|---|---|---|---|
| TFW-05 | 0.1 | architect | Python 3 [가정: 3.10 이상], Modbus TCP 클라이언트 라이브러리 [가정: pymodbus] | NFR-002, NFR-003, NFR-004, NFR-010, NFR-011, NFR-023, T-03~T-12, T-14 |

> 시험 펌웨어(TFW-01~04)를 Modbus TCP로 제어하고 부하를 만들며 결과를 수집해 G3 보고서를 만든다. 제품 시험 자동화(MOD-22)의 전신이며 ④에서 MOD-22로 흡수할 수 있다.

## 1. 책임 (이 모듈이 하는 일 / 하지 않는 일)
- 한다: 시나리오 파일(YAML [가정])로 T-03~T-12 실행 순서·조건 정의, 시험 창 쓰기/읽기, Modbus 부하 생성(동시 연결 N개, FC03 125레지스터 연속, FC16 쓰기, 요청 간격 0~), 결과 수집(시험 창 카운터, 응답 시간 분포), 로직 분석기 내보내기 파일(CSV [가정])에서 펄스 수·주파수·지연 계산, 합격 판정과 보고서(Markdown) 생성.
- 하지 않는다: 로직 분석기 장치 제어(수동 캡처 후 파일 입력 [가정]), 펌웨어 빌드·플래시.

## 2. 의존성
- 사용: Modbus TCP 라이브러리, PyYAML [가정], 표준 라이브러리(statistics, csv).
- 사용하는 쪽: 시험 담당(사용자·coder), G3 보고서.

## 3. 입출력 정의
| 구분 | 이름 | 타입/단위 | 범위 | 설명 |
|---|---|---|---|---|
| 입력 | scenario.yaml | 파일 | | T-ID별 조건(축, 주파수, 반복, 부하, 경로) |
| 입력 | target | IP:port | 192.168.0.220:502 | |
| 입력 | la_export.csv | 파일 | | 로직 분석기 엣지 시각(채널별) |
| 출력 | results/<T-ID>_<시각>.json | 파일 | | 원자료 |
| 출력 | report_G3_pretest.md | 파일 | | T-ID별 조건·측정값·합격 여부·미확인 사항 |
| 오류 | 예외 응답·타임아웃 | | | 기록 후 계속(부하 시험), 제어 명령은 3회 재시도 [가정] |

공개 API(시그니처 수준):
```python
def run_scenario(path: str, target: str) -> dict: ...
def load_clients(target: str, n: int, fc: str, qty: int, interval_ms: float, duration_s: float) -> LoadStats: ...
def count_edges(la_csv: str, channel: str) -> int: ...
def freq_stats(la_csv: str, channel: str) -> FreqStats: ...          # min/avg/max frequency, period jitter
def latency(la_csv: str, trigger_ch: str, pulse_ch: str) -> LatStats: # estop edge -> last pulse
def make_report(results_dir: str, out_md: str) -> None: ...
```

## 4. 제약 조건
- 부하 클라이언트는 프로세스(또는 스레드) 분리로 서로 막지 않게 한다.
- 합격 판정 규칙은 architecture §6.1 표를 그대로 코드화하고, 기준이 "기록"인 항목은 판정 없이 값만 보고한다.
- 보고서에는 펌웨어 빌드 해시, sdkconfig 차이(IRAM 설정 등), 칩 리비전(TFW-01 0x7036), 시험 일시·장비를 적는다.
- 기존 실적으로 대체한 T-01·T-02·T-14(1축)는 보고서에 "기존 실적으로 대체(D-028)"와 근거 문서(servo1axis CURRENT_STATE.md)를 적는다.

## 5. 완료 기준 (각 항목은 단위 테스트로 검증 가능해야 함)
| # | 기준 | 검증 방법 |
|---|---|---|
| 1 | `count_edges`·`freq_stats`가 합성 CSV(알려진 엣지 수·주기)에서 정확 | pytest |
| 2 | `load_clients` 4연결로 지정 시간 부하, 응답 시간 분포 산출 | pytest(로컬 모의 Modbus 서버) |
| 3 | 시나리오 T-03~T-09 실행·결과 JSON 생성 | HIL |
| 4 | 보고서에 14개 T-ID 전부 표시(대체·미실시 포함) | pytest(보고서 생성) |

## 6. 미확인·가정
- Python 버전, Modbus 라이브러리, YAML, 로직 분석기 내보내기 형식 [가정].
- 로직 분석기 기종·샘플링 속도 미정(architecture §6.2).
