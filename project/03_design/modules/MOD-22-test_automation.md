# 설계→코딩 인터페이스: test_automation (HIL·실측 자동화)

| 모듈 ID | 버전 | 작성 | 대상 언어/플랫폼 | 추적 SRS ID |
|---|---|---|---|---|
| MOD-22 | 0.1 | architect | Python 3 + pytest [가정] (PC) | NFR-001, NFR-002, NFR-003, NFR-004, NFR-005, NFR-006, NFR-010, NFR-011, NFR-013, NFR-023, STD-003, FR-004, FR-022 |

## 1. 책임 (이 모듈이 하는 일 / 하지 않는 일)
- 한다: G3 전 실측(T-01~T-08, T-09는 수동 검사 기록 양식만)과 ④·⑤ HIL 회귀 시험을 자동화한다. 구성 요소: (1) Modbus 클라이언트·부하 발생기(N 연결 × 요청률, 읽기·쓰기 혼합, 응답 시간 분포 기록), (2) 계측기 어댑터 추상화(펄스 카운터·주파수 카운터·로직 분석기 — 기종 미확인, 인터페이스만 정의), (3) I/O 모의 어댑터(원점·리밋·알람·완료·비상정지 입력을 릴레이/디지털 출력 장치로 구동 — 기종 미확인), (4) 시험 시나리오(아래 표), (5) 결과 저장(CSV/JSON)과 요약 보고서(Markdown, ⑤·G3 근거 자료).
- 하지 않는다: 시험 케이스 설계 문서(⑤ test-designer), 펌웨어 기능.

## 2. 의존성
- 사용: MOD-20 `regmap.py`, MOD-21 `Controller`(재사용), pytest, 계측기 드라이버(미확인: VISA/벤더 SDK).
- 사용하는 쪽: ③(G3 전 실측), ④ 회귀, ⑤ 검증.

## 3. 입출력 정의
| 구분 | 이름 | 타입/단위 | 범위 | 설명 |
|---|---|---|---|---|
| 입력 | 장비 설정 | YAML | | 대상 IP, 계측기 종류·주소, I/O 장치, 케이블 길이 |
| 입력 | 시나리오 파라미터 | | 주파수, 펄스 수, 반복, 부하 수준 | |
| 출력 | 측정 기록 | CSV/JSON | | 시각, 지령, 계측값, 장치 진단값(underrun_count, max_isr_us 등) |
| 출력 | 합격 판정 | PASS/FAIL/RECORD | | 목표값 미확인 항목은 RECORD(측정값만) |

공개 API(시그니처 수준):
```python
class PulseCounter(Protocol):
    def reset(self, ch: int) -> None: ...
    def read(self, ch: int) -> int: ...
    def frequency(self, ch: int, gate_s: float) -> float: ...
class IoSimulator(Protocol):
    def set(self, signal: str, active: bool) -> None: ...
    def pulse(self, signal: str, width_ms: float) -> None: ...
class LoadGenerator:
    def __init__(self, host: str, clients: int, rate_hz: float, mix: dict): ...
    def start(self) -> None: ...
    def stop(self) -> "LatencyStats": ...
def run_scenario(name: str, cfg: dict) -> "Result": ...
```

시나리오(G3 전 실측):
| ID | 내용 | 자동화 범위 |
|---|---|---|
| T-01 | 2축 동시 ≥ 200 kpps 연속, 가감속 동작 | 주파수 측정·기록 |
| T-02 | 반복 위치결정(절대·상대·JOG) 펄스 수 비교 | 카운터 대조, 오차 0 판정 |
| T-03 | T-01 + Modbus TCP 최대 부하(연결 수 = 장비 최대) | 부하 발생 + 언더런·지터 기록 |
| T-04 | T-01 + 파라미터 저장 반복 | SAVE 반복 + 언더런 기록 |
| T-05 | 비상정지 엣지 → 마지막 펄스 시간 | 로직 분석기 캡처 해석 |
| T-06 | 24 V OC 출력 단계별 주파수 | 파형 캡처 기록(판정은 사람) |
| T-07 | 차동 출력 5 m 파형·펄스 수 | 카운터 대조 + 캡처 |
| T-08 | 절연 입력 응답 지연·판정 전압 | 가변 전원 제어(장비 미확인) + 지연 기록 |
| T-09 | 기구 수납 | 수동 검사 기록 양식 |

## 4. 제약 조건
- 목표값이 "미확인"인 요구사항(NFR-003 지터, NFR-011 응답 시간 등)은 합격 판정 없이 측정값을 기록한다(SRS 원칙: 측정값 공개).
- 장시간 시험(NFR-004 24 h, NFR-013)은 중단·재개 가능, 중간 기록 저장.
- 결과 파일에 펌웨어 버전·regmap_version·장비 구성을 함께 기록(재현성).

## 5. 완료 기준 (각 항목은 단위 테스트로 검증 가능해야 함)
| # | 기준 | 검증 방법 |
|---|---|---|
| 1 | 모의 계측기·모의 Modbus 서버로 전 시나리오 실행 성공 | pytest(오프라인) |
| 2 | 부하 발생기가 설정 연결 수·요청률 유지, 지연 분포 산출 | pytest + HIL |
| 3 | T-01~T-05·T-07 보고서 자동 생성 | HIL |
| 4 | 결과에 장비·버전 메타데이터 포함 | 검사 |

## 6. 미확인·가정
- 계측기·I/O 모의 장치 기종 미확인 → 어댑터 인터페이스만 고정. pytest [가정].
- 시험 펌웨어(handoff §7 1단계)와 함께 써야 G3 전 실측이 가능하다.
