# 설계→코딩 인터페이스: param_store (파라미터 관리·비휘발 저장)

| 모듈 ID | 버전 | 작성 | 대상 언어/플랫폼 | 추적 SRS ID |
|---|---|---|---|---|
| MOD-12 | 0.1 | architect | C(MCU 무관) | FR-015, FR-016, FR-022, FR-023(c) |

## 1. 책임 (이 모듈이 하는 일 / 하지 않는 일)
- 한다: 축 파라미터(interface-spec §5.6)·시스템 설정(§5.3 0x0110~)·네트워크 설정(§5.10)의 RAM 사본 관리, 쓰기 시 범위·관계 검사(예: start_speed ≤ max_speed), "정지 중에만 반영" 항목의 반영 시점 관리, 비휘발 저장(이중 사본 A/B + 순번 + CRC32), 부팅 시 적재(유효한 최신 사본, 둘 다 손상이면 기본값 + E-PARAM), 공장 초기화, 자동 지연 저장. 파라미터 설명자(이름·주소·형·범위·기본값·반영 시점)는 `regmap.yaml`에서 생성된 표(MOD-20)를 쓴다.
- 하지 않는다: Modbus 주소 해석(MOD-14), PC 측 백업 파일 형식(MOD-21: PC 도구가 레지스터를 읽어 파일로 저장·복원).

## 2. 의존성
- 사용: MOD-06 hal_nvm, MOD-01(뮤텍스), MOD-20 생성물(`param_desc_table`).
- 사용하는 쪽: MOD-09, MOD-10, MOD-11, MOD-14, MOD-15(네트워크·max_clients), MOD-19(부팅 적재).

## 3. 입출력 정의
| 구분 | 이름 | 타입/단위 | 범위 | 설명 |
|---|---|---|---|---|
| 입력 | param id | uint16_t | 생성 표 인덱스 | |
| 입력 | value | int32_t/uint32_t | 설명자 min~max | |
| 출력 | value | | | 현재 RAM 값 |
| 출력 | dirty | bool | | 미저장 변경 존재(sys_status b3) |
| 출력 | load_result | enum | OK_A, OK_B, DEFAULTS(E-PARAM) | |
| 오류 | ps_err_t | | PS_ERANGE, PS_ERELATION, PS_EBUSY_AXIS(운전 중 변경 불가 항목), PS_ENVM | Modbus 예외 03/04로 변환(MOD-14) |

공개 API(시그니처 수준):
```c
ps_err_t ps_init_and_load(uint8_t axis_count, ps_load_result_t *res);
ps_err_t ps_get(uint16_t id, uint8_t axis, int32_t *out);
ps_err_t ps_set(uint16_t id, uint8_t axis, int32_t value, bool axis_idle);
ps_err_t ps_save(void);                 /* queued to SERVICE task, returns immediately */
ps_err_t ps_load_defaults(int8_t axis); /* -1 = all */
bool     ps_dirty(void);
bool     ps_saving(void);
void     ps_service_tick(void);         /* SERVICE task: auto-save after 2 s idle [assumed] */
const ps_axis_t *ps_axis(uint8_t axis); /* read-only struct view for MOTION side, updated atomically */
```

저장 형식(의사코드):
```
record = { magic, layout_version(regmap_version), seq++, model_id, payload[...], crc32 }
save: write to the slot with older seq (A/B alternate) -> read back verify -> done
load: pick valid record with highest seq; layout_version mismatch -> migrate known fields, defaults for new
```

## 4. 제약 조건
- 저장(지우기·쓰기)은 SERVICE 태스크에서만(MOD-06 §4). 저장 중에도 운전은 계속된다(T-04 합격 조건).
- MOTION 측 읽기는 잠금 없이(축별 구조체 이중 버퍼 교체) [가정]. 쓰기는 뮤텍스.
- 전원 차단 내성: 저장 중 전원 차단 시 이전 사본이 남아 있어야 한다(A/B 교대).
- 쓰기 빈도 제한: 자동 저장은 마지막 쓰기 후 2 s [가정], 최소 간격 10 s [가정](수명 보호).

## 5. 완료 기준 (각 항목은 단위 테스트로 검증 가능해야 함)
| # | 기준 | 검증 방법 |
|---|---|---|
| 1 | 전 파라미터 범위 경계값 수락, 경계 밖 거부 | 호스트 단위 테스트(생성 표 기반 자동 생성 케이스) |
| 2 | 관계 위반(start_speed > max_speed 등) 거부 | 호스트 단위 테스트 |
| 3 | 저장 → 전원 재투입 → 값 일치(미확인 회 반복, ⑤에서 횟수 정함) | HIL(FR-015) |
| 4 | 저장 중 전원 차단 100회 [가정] 후 항상 유효 사본 적재 | HIL |
| 5 | 두 사본 손상 시 기본값 적재 + E-PARAM, 기동 금지 | 호스트 시뮬(손상 주입) |
| 6 | 운전 중 "정지 중만 반영" 항목 쓰기 거부 | 호스트 시뮬 |

## 6. 미확인·가정
- 파라미터 목록은 제안안(DC-16, FR-023(c)) — G3 확인 후 regmap.yaml 확정.
- 자동 저장 시간·최소 간격 [가정].
- 레이아웃 버전 이동(migrate) 범위는 ④에서 정의.
