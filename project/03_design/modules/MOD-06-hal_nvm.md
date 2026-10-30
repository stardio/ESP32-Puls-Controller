# 설계→코딩 인터페이스: hal_nvm (비휘발 저장 HAL)

| 모듈 ID | 버전 | 작성 | 대상 언어/플랫폼 | 추적 SRS ID |
|---|---|---|---|---|
| MOD-06 | 0.1 | architect | C — port_stm32f4(내장 플래시, 조건부 FRAM FM24CL64B) / port_esp32(SPI 플래시 파티션) | FR-015, FR-018, FR-021, NFR-004(간섭) |

## 1. 책임 (이 모듈이 하는 일 / 하지 않는 일)
- 한다: 논리 영역(region) 단위로 읽기·지우기·쓰기를 제공한다. 영역: NVM_PARAM_A, NVM_PARAM_B, NVM_ALARM_LOG, NVM_SEQ(FR-013), NVM_FW_SLOT_A, NVM_FW_SLOT_B, NVM_BOOT_META. 영역별 크기·지우기 단위를 질의할 수 있다. 백엔드(내장 플래시/FRAM)는 영역별로 선택된다.
- 하지 않는다: 데이터 형식·CRC·이중화 정책(MOD-12·13·17).

## 2. 의존성
- 사용: MOD-01, (조건부) I2C 드라이버(FRAM).
- 사용하는 쪽: MOD-12, MOD-13, MOD-17, MOD-18.

## 3. 입출력 정의
| 구분 | 이름 | 타입/단위 | 범위 | 설명 |
|---|---|---|---|---|
| 입력 | region | enum | 위 7종 | |
| 입력 | offset, len | uint32_t, byte | 영역 크기 이내, 쓰기 정렬은 `write_align` | |
| 출력 | info | 구조체 | size, erase_unit, write_align, backend | |
| 출력 | busy | bool | | 지우기·쓰기 진행 여부 |
| 오류 | hal_err_t | | HAL_EINVAL(범위·정렬), HAL_EIO(검증 실패), HAL_EBUSY | |

공개 API(시그니처 수준):
```c
typedef struct { uint32_t size, erase_unit, write_align; uint8_t backend; } hal_nvm_info_t;
hal_err_t hal_nvm_info(hal_nvm_region_t r, hal_nvm_info_t *out);
hal_err_t hal_nvm_read(hal_nvm_region_t r, uint32_t off, void *buf, uint32_t len);
hal_err_t hal_nvm_erase(hal_nvm_region_t r, uint32_t off, uint32_t len);   /* blocking, SERVICE task only */
hal_err_t hal_nvm_write(hal_nvm_region_t r, uint32_t off, const void *buf, uint32_t len);
bool      hal_nvm_busy(void);
```

## 4. 제약 조건
- 호출 문맥: SERVICE 등급(L4) 태스크에서만. ISR 호출 금지.
- 간섭: 내장 플래시 지우기·쓰기 중 플래시 명령 인출이 멈출 수 있다(R-13, 지우기 시간 미확인). 포트는 (a) L1·L2 핵심 코드를 RAM에 두고(MOD-01 매크로), (b) 지우기를 가능한 작은 단위로 나누고, (c) 지우기 중 최악 L1 지연을 측정할 수 있게 계측 훅을 둔다. T-04 불합격 시 PARAM·ALARM_LOG 영역을 FRAM 백엔드로 옮긴다(API 불변).
- 쓰기 수명: 알람 이력 기록 빈도 대비 수명 계산은 미수행(미확인) → MOD-13이 쓰기 횟수를 제한한다.
- 영역 배치(플래시 맵)는 MCU 플래시 크기·섹터 구조에 따름(STM32F407VGT6 값 데이터시트 미대조, 미확인).

## 5. 완료 기준 (각 항목은 단위 테스트로 검증 가능해야 함)
| # | 기준 | 검증 방법 |
|---|---|---|
| 1 | 영역별 쓰기 → 전원 재투입 → 읽기 일치 | HIL |
| 2 | 범위·정렬 위반 시 HAL_EINVAL | 단위 테스트(RAM 모의 백엔드) |
| 3 | 지우기 중 펄스 엔진 언더런 0 | HIL T-04 |
| 4 | 영역 경계 밖 쓰기 불가(다른 영역 손상 없음) | 단위 테스트 |

## 6. 미확인·가정
- 플래시 크기·섹터 크기·지우기 시간 미확인. 영역 크기는 ④에서 데이터시트 대조 후 확정.
- FRAM 채택은 조건부(BOM NVM, USD 2.82 @1).
