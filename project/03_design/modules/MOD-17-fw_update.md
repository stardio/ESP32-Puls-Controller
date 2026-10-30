# 설계→코딩 인터페이스: fw_update (현장 펌웨어 갱신·부트로더)

| 모듈 ID | 버전 | 작성 | 대상 언어/플랫폼 | 추적 SRS ID |
|---|---|---|---|---|
| MOD-17 | 0.1 | architect | C — 앱 측 수신부(MCU 무관) + 부트로더(포트별: STM32 자체 부트로더 / ESP32는 ESP-IDF OTA 사용 [가정]) | FR-018, NFR-022(맵 변경 이력) |

## 1. 책임 (이 모듈이 하는 일 / 하지 않는 일)
- 한다: (앱 측) Modbus 갱신 창(interface-spec §5.11)으로 이미지 블록 수신, 비활성 슬롯에 기록, CRC32·이미지 헤더(매직, 크기, 버전, 대상 모델·MCU 식별, regmap_version) 검증, 커밋 시 부트 메타에 "새 슬롯 시험 부팅" 표시. 새 이미지가 기동 후 자가 점검을 통과하면 "확정" 표시. (부트로더) 두 슬롯 중 확정된 최신 유효 슬롯을 부팅, 시험 부팅이 N회 [가정: 3회] 확정 없이 실패하면 이전 슬롯으로 복귀.
- 하지 않는다: 이미지 전송 UI(MOD-21), 갱신 중 운전(금지).

## 2. 의존성
- 사용: MOD-06(FW_SLOT_A/B, BOOT_META), MOD-14(갱신 창 레지스터), MOD-09(전 축 정지 확인), MOD-01(재부팅).
- 사용하는 쪽: MOD-21 pc_tool.

## 3. 입출력 정의
| 구분 | 이름 | 타입/단위 | 범위 | 설명 |
|---|---|---|---|---|
| 입력 | BEGIN(size, crc32) | U32, U32 | size ≤ 슬롯 크기 | |
| 입력 | block(index, 240 B) | | 순차 | FC16 1회 = 1블록 |
| 입력 | COMMIT / ABORT | | | |
| 출력 | fwu_status | U16 | 0 대기, 1 수신 중, 2 검증 중, 3 완료(재부팅 대기), 0x10 운전 중 거부, 0x11 크기 초과, 0x12 순서 오류, 0x13 CRC 불일치, 0x14 대상 모델/MCU 불일치 | |
| 오류 | 위 0x1x | | | |

공개 API(시그니처 수준):
```c
uint16_t fwu_begin(uint32_t size, uint32_t crc32);
uint16_t fwu_write_block(uint32_t index, const uint8_t *data, size_t len);
uint16_t fwu_commit(void);
void     fwu_abort(void);
uint16_t fwu_status(void);
void     fwu_confirm_running_image(void);   /* called by MOD-19 after self-test */
```

플래시 배치(개념):
```
[ bootloader ][ BOOT_META ][ FW_SLOT_A ][ FW_SLOT_B ][ PARAM_A ][ PARAM_B ][ ALARM_LOG ][ SEQ ]
 each app image linked for its slot (or position-independent) ; sizes depend on MCU flash map (unknown)
```

## 4. 제약 조건
- 갱신 시작 조건: 전 축 정지·서보 OFF [가정], 잠금 키(0x0102) 필요.
- 전원 차단 내성: 어느 시점에 전원이 끊겨도 확정 슬롯 하나는 항상 부팅 가능(FR-018).
- 플래시 용량: 앱 2슬롯 + 부트로더 + 데이터 영역 ≤ MCU 플래시. STM32F407VGT6 플래시 크기·섹터 구조 미확인 → 앱 크기 추정 후 ④에서 판정. 부족하면 "단일 슬롯 + 외부 저장 사본" 등으로 변경(G3 보고).
- 쓰기는 SERVICE 태스크(펄스 엔진 간섭 없음 확인은 T-04 범위 확장).

## 5. 완료 기준 (각 항목은 단위 테스트로 검증 가능해야 함)
| # | 기준 | 검증 방법 |
|---|---|---|
| 1 | 정상 갱신 후 새 버전 부팅, fw_version 갱신 | HIL |
| 2 | 전송 중 임의 시점 전원 차단 20회 [가정] → 항상 부팅 가능, 재갱신 가능 | HIL(FR-018) |
| 3 | 확정 전 새 이미지가 기동 실패(시험용 고장 이미지) → 이전 슬롯 복귀 | HIL |
| 4 | CRC·모델 불일치 이미지 거부 | 호스트 단위 테스트 + HIL |
| 5 | 운전 중 BEGIN 거부 | HIL |

## 6. 미확인·가정
- 플래시 크기·섹터·이중 슬롯 가능 여부 미확인(DC-14 포트별). ESP32는 ESP-IDF OTA 파티션·롤백 기능 사용 [가정, 문서 대조 전].
- 시험 부팅 허용 횟수 3 [가정]. 갱신 FR-018은 권장이므로 구현 순서 후순위.
