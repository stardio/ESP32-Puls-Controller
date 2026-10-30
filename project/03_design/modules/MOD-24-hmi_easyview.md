# 설계→코딩 인터페이스: hmi_easyview (이지뷰 패널용 기본 화면 데이터)

| 모듈 ID | 버전 | 작성 | 대상 언어/플랫폼 | 추적 SRS ID |
|---|---|---|---|---|
| MOD-24 | 0.2 | architect | Weintek EasyBuilder(Pro로 추정, 버전 미확인) 프로젝트(.emtp) + Python 태그 변환 스크립트 | FR-024, FR-023, CON-012, D-021, D-022 |

> v0.2 개정: servo1axis의 `MT8051iP/ESP32HMI_대우서보.emtp`(Weintek MT8051iP용, 1축)[E9]와 기존 맵 문서[E6]를 기준선으로 삼는다. 분류: **수정 후 재사용**. 기존 맵 문서는 이 패널을 "EasyView"로, 작화 도구를 "EasyBuilder"로 부른다[E6] → **이지뷰(D-021) = Weintek EasyView/EasyBuilder 계열로 판단 [가정, 사용자 확인 필요]**. .emtp는 바이너리라 내용(화면 수·태그)을 열람하지 않았다.

## 1. 책임 (이 모듈이 하는 일 / 하지 않는 일)
- 한다:
  1. **호환 기준 프로젝트(1축)**: 기존 .emtp를 복사해 보존하고, 레거시 창(interface-spec §5.0.1)만으로 동작함을 확인한다(펌웨어 변경 후 회귀 시험 기준).
  2. **기본 틀 프로젝트(1축·2축)**: MOD-23 공통 정의(S00~S06·P01·P02)를 대상 기종별로 작화. 2축 화면과 신규 기능(원점복귀·JOG·알람·파라미터)은 축 영역(0x1000×n)·시스템 영역(0x0020~)을 쓴다.
  3. 통신 설정: MODBUS TCP/IP 장치 IP = 컨트롤러 IP(기본 192.168.0.220, D-027), 포트 502, 유닛 1, 32-bit unsigned/signed 형 + **하위 워드 먼저**(기존 설정과 동일[E6]), 주소 표기 4x-N = 주소 N−1[E6].
  4. 태그 변환 스크립트: MOD-20 `hmi_tags.csv` → EasyBuilder 태그 가져오기 형식(형식 미확인).
- 하지 않는다: 공통 정의 변경(MOD-23), 기본 틀 외 화면(D-022 추가 비용).

## 2. 의존성
- 사용: MOD-23, MOD-20 `hmi_tags.csv`, 기존 .emtp[E9], EasyBuilder(버전 미확인), 실 패널(MT8051iP 1대 보유 [가정: servo1axis 시험에 사용[E5]]).
- 사용하는 쪽: 고객 배포, ⑥ 매뉴얼.

## 3. 입출력 정의
| 구분 | 이름 | 타입/단위 | 범위 | 설명 |
|---|---|---|---|---|
| 입력 | ESP32HMI_대우서보.emtp | EasyBuilder 프로젝트 | MT8051iP, 1축 | 기준선(읽기 전용 원본은 servo1axis) |
| 입력 | hmi_tags.csv | CSV | | 레거시 창 + 신규 영역 |
| 입력 | 대상 기종 | | Q26, PS-29 | D-017은 5/7인치. MT8051iP는 4.3인치·단종 보고[S16] |
| 출력 | compat_1axis_MT8051iP.emtp | 프로젝트 | | 호환 회귀 기준 |
| 출력 | base_{1axis,2axis}_<기종>.emtp | 프로젝트 | | 기본 틀 |
| 출력 | ev_tags_import.* | 가져오기 형식(미확인) | | |
| 오류 | 변환 불가 태그 | | | 경고 목록 |

공개 API(시그니처 수준):
```python
def convert_tags_easyview(common_csv: str, out_path: str, word_order: str = "low_first",
                          addr_base: int = 1) -> list[str]: ...
# addr_base=1: EasyBuilder 4x-N == protocol address N-1 (confirmed by servo1axis map)
```

## 4. 제약 조건
- 호환 프로젝트는 레거시 창 주소(HR 0~23, 코일 0~3, DI 0~4)만 사용해야 한다. 신규 영역 주소를 섞지 않는다.
- START 버튼은 쓰기 주소 0x-1(코일 0) 순간 동작, 읽기 주소 분리 시 1x-1(기존 규칙[E6]).
- 패널 요청 주기·동시 접속은 `max_clients`·NFR-010 측정값에 맞춘다.
- 화면 문구 한국어. 비상정지 팝업에 보조 정지 고지(NFR-020).

## 5. 완료 기준 (각 항목은 단위 테스트로 검증 가능해야 함)
| # | 기준 | 검증 방법 |
|---|---|---|
| 1 | 태그 변환: 공통 CSV 전 항목 변환, 레거시 주소 = 기존 맵 문서 값 | pytest |
| 2 | 호환 프로젝트 + 신규 펌웨어: Position·Speed·가감속 쓰기, START, Force Stop, 방향, 상태 표시가 기존 펌웨어와 같게 동작 | 시연(MT8051iP) |
| 3 | 기본 틀 프로젝트: MOD-23 체크리스트 (a)~(d) 전 항목, 1축·2축 | 시연(FR-024) |
| 4 | 32비트 위치값(음수 포함) 표시 정확(하위 워드 먼저) | 시연 |
| 5 | JOG 누름/뗌·통신 단절 시 정지 | 시연 |

## 6. 미확인·가정
- 이지뷰 = Weintek 계열 여부 [가정](사용자 확인). EasyBuilder 버전, 태그 가져오기 형식 미확인.
- 5/7인치 대상 기종 미정(DC-16, PS-29). MT8051iP 단종 정보는 유통사 기재[S16].
- .emtp 내용(화면 구성·태그 수) 미열람 — ④에서 EasyBuilder로 열어 목록화.
