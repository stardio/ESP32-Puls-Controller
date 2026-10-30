# 설계→코딩 인터페이스: regmap_gen (레지스터 맵 단일 원본·코드/문서 생성기)

| 모듈 ID | 버전 | 작성 | 대상 언어/플랫폼 | 추적 SRS ID |
|---|---|---|---|---|
| MOD-20 | 0.1 | architect | Python 3 (PC, 빌드 도구) | NFR-022, NFR-016, FR-004, FR-023, FR-024, STD-003 |

## 1. 책임 (이 모듈이 하는 일 / 하지 않는 일)
- 한다: `regmap.yaml`(레지스터 맵 단일 원본, interface-spec §5 내용)을 읽어 검사하고 다음을 생성한다. (1) 펌웨어 C 표 `regmap_table.c/.h`(MOD-14·12용 설명자), (2) Python 모듈 `regmap.py`(MOD-21·22용 주소·형·범위 상수), (3) 한국어 레지스터 맵 문서(Markdown, NFR-022 공개 문서의 원본, ⑥ tech-writer 입력), (4) HMI 공통 태그 목록 CSV(MOD-23 → MOD-24·25 변환 입력), (5) 맵 변경 이력(이전 버전 대비 추가·삭제·이동 주소 목록).
- 검사: 주소 중복·겹침, 32비트 항목 짝수 정렬 여부 [가정: 강제하지 않고 경고], 읽기 묶음 125레지스터 한도, 축1/축2 상대 배치 동일성, 기본값이 범위 안인지, regmap_version 증가 누락(주소 변경 시).
- 하지 않는다: 펌웨어 런타임 동작. 이 도구의 출력 파일은 수작업 편집 금지.

## 2. 의존성
- 사용: Python 표준 라이브러리 + YAML 파서(PyYAML [가정]).
- 사용하는 쪽: 펌웨어 빌드(MOD-12·14), MOD-21, MOD-22, MOD-23~25, ⑥ 문서.

## 3. 입출력 정의
| 구분 | 이름 | 타입/단위 | 범위 | 설명 |
|---|---|---|---|---|
| 입력 | regmap.yaml | YAML | | 항목: name, addr(축 상대 또는 절대), type(U16/S16/U32/S32/BITS16), access(R/RW/W), unit, min, max, default, save(bool), apply(immediate/idle/reboot), group(운전 데이터/운전 조작/파라미터/알람/시스템), label_ko, desc_ko, srs_ids, fc |
| 입력 | --prev 이전 yaml | YAML | | 변경 이력 산출용 |
| 출력 | regmap_table.c/.h | C | | |
| 출력 | regmap.py | Python | | |
| 출력 | regmap_ko.md | Markdown | | NFR-022 항목(주소, 데이터형, 단위, R/W, 범위, 기능 코드) 전부 포함 |
| 출력 | hmi_tags.csv | CSV | | tag, addr_0based, addr_4x, type, words, word_order, access, group, label_ko, scale |
| 오류 | 종료 코드 ≠ 0 | | | 검사 실패 항목 목록 출력 |

공개 API(시그니처 수준):
```python
def load_regmap(path: str) -> "RegMap": ...
def validate(rm: "RegMap", max_read_qty: int = 125) -> list["Issue"]: ...
def gen_c(rm: "RegMap", out_dir: str) -> None: ...
def gen_py(rm: "RegMap", out_path: str) -> None: ...
def gen_doc_ko(rm: "RegMap", out_path: str) -> None: ...
def gen_hmi_tags(rm: "RegMap", out_path: str, axis_count: int) -> None: ...
def diff(prev: "RegMap", cur: "RegMap") -> "ChangeLog": ...
# CLI: python -m regmap_gen regmap.yaml --out build/ [--prev old.yaml]
```

## 4. 제약 조건
- 결정성: 같은 입력 → 바이트 단위로 같은 출력(빌드 재현성).
- 펌웨어 빌드는 생성물 검사(해시 또는 재생성 비교)로 수작업 편집을 막는다 [가정].
- 문서 언어 한국어(D-004, NFR-016).

## 5. 완료 기준 (각 항목은 단위 테스트로 검증 가능해야 함)
| # | 기준 | 검증 방법 |
|---|---|---|
| 1 | interface-spec §5 전 항목이 yaml에 있고 생성 문서와 일치 | 단위 테스트(pytest) + 검사(문서 대조) |
| 2 | 주소 겹침·범위 밖 기본값·축 배치 불일치 주입 시 검출 | pytest |
| 3 | 생성 C 표를 MOD-14 호스트 테스트가 그대로 사용 | 빌드 + 단위 테스트 |
| 4 | 주소 변경 시 regmap_version 미증가 → 실패 | pytest |
| 5 | 펌웨어 실제 응답과 생성 문서 일치(NFR-022 검증) | HIL(MOD-22: 문서 기반 전 주소 접근) |

## 6. 미확인·가정
- YAML 형식·PyYAML [가정]. HMI 메이커별 가져오기 형식은 미확인 → 공통 CSV까지만 이 모듈이 맡고, 변환은 MOD-24·25.
