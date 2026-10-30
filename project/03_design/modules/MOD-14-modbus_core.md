# 설계→코딩 인터페이스: modbus_core (Modbus PDU 처리·레지스터 표)

| 모듈 ID | 버전 | 작성 | 대상 언어/플랫폼 | 추적 SRS ID |
|---|---|---|---|---|
| MOD-14 | 0.2 | architect | C(MCU 무관, 전송 계층 무관) | FR-004, FR-005, FR-016, FR-021, FR-022, FR-023, NFR-010, NFR-022, STD-003 |

> v0.2 개정: servo1axis `process_request`(FC01/02/03/05/06/15/16, 예외 응답, 후보 배열 검증 후 일괄 반영)[E2]를 **수정 후 재사용**한다. 고정 배열(HR 32·코일 8) 대신 MOD-20 생성 표를 쓰고, 레거시 창(interface-spec §5.0.1)을 표의 한 영역으로 포함한다. 워드 순서는 **하위 워드 먼저**(DC-15), FC02 추가.

## 1. 책임 (이 모듈이 하는 일 / 하지 않는 일)
- 한다: Modbus PDU 해석·응답 생성. 지원 기능 코드 01·**02**·03·04·05·06·15·16, 예외 01~04(interface-spec §5.1). 레거시 창(HR 0x0000~0x001F, 코일 0~7, DI 0~7)의 기존 동작 재현(쓰기 가능 주소: HR 0~3·20~23, 시작 속도 쓰기 거부, 방향 코일 저장). 생성된 레지스터 표(MOD-20)로 주소 → 항목 매핑, 읽기 시 스냅샷 일관성(32비트 하위 워드 먼저), 쓰기 시 형·범위·속성 검사, 32비트 반쪽 쓰기 거부, 1축 모델 축2 영역 예외 02. 명령 레지스터 쓰기는 MOD-09/10/12/17 명령으로 변환. 요청 출처(conn_id)를 명령에 붙여 JOG 소유·통신두절 판정에 쓰게 한다.
- 하지 않는다: MBAP·RTU 프레이밍(MOD-15·16), 소켓·UART(MOD-04·05).

## 2. 의존성
- 사용: MOD-20 생성 표(`regmap_table.c`), MOD-09(스냅샷·명령 큐), MOD-10, MOD-12, MOD-13, MOD-17(갱신 창), MOD-19(모델 정보).
- 사용하는 쪽: MOD-15, MOD-16.

## 3. 입출력 정의
| 구분 | 이름 | 타입/단위 | 범위 | 설명 |
|---|---|---|---|---|
| 입력 | req_pdu | uint8_t[], byte | ≤ 253 B(규격 PDU 최대 [규격 원문 대조 전]) | |
| 입력 | ctx | mb_ctx_t | conn_id, transport(TCP/RTU), unit_id | |
| 출력 | rsp_pdu | uint8_t[] | ≤ 253 B | 정상 또는 예외 응답 |
| 출력 | 통계 | 요청 수, 예외 수(코드별) | | 진단 |
| 오류 | 예외 코드 | 01 미지원 FC, 02 주소, 03 값/개수, 04 처리 실패 | | interface-spec §5.1 |

공개 API(시그니처 수준):
```c
typedef struct { int8_t conn_id; uint8_t transport; uint8_t unit_id; } mb_ctx_t;
void   mb_core_init(uint8_t axis_count);
size_t mb_core_handle(const mb_ctx_t *ctx, const uint8_t *req, size_t req_len, uint8_t *rsp, size_t rsp_cap);

/* generated table entry (from regmap.yaml) */
typedef struct { uint16_t addr; uint8_t words; uint8_t type; uint8_t access; uint8_t flags;
                 int32_t min, max; mb_get_fn get; mb_set_fn set; } mb_reg_t;
```

처리(의사코드):
```
FC03/04 (start, qty): check qty 1..125 ; for each addr: entry = lookup(addr) else exc 02
    take snapshot once per request (ax_get_snapshot for touched axes) -> encode each word big-endian, 32-bit values low word first (DC-15)
FC02 (start, qty): legacy discrete inputs 0..7 only (running, link, client, complete, fault) else exc 02
legacy window: writable HR 0..3, 20..23 only; single-word write allowed, validate full 32-bit set like existing validate_control_registers()
FC06/16: for each addr: entry RW? else exc 02 ; 32-bit entries must be written fully in same request else exc 03
    validate all values first (atomic request: all-or-nothing) -> then apply via set() -> exc 03/04 on failure
FC01/05/15: coil map (JOG+/-, STOP, SERVO_ON) -> translated to axis commands
command registers (sys_command, axis_command): write enqueues command with ctx.conn_id; result readable at *_cmd_result
```

## 4. 제약 조건
- 응답 시간: 요청 처리 시간 ≤ 1 ms [가정](NFR-010 값 미확인, 측정값 공개). 명령은 큐 투입까지만 동기, 실행 결과는 cmd_result로 비동기 확인.
- 한 요청 안의 쓰기는 전부 검사 후 일괄 적용(일부만 반영 금지) [가정].
- 스레드 안전: COMM 태스크 여러 개(TCP 연결별 또는 RTU)에서 동시에 호출될 수 있다 → 읽기는 스냅샷(잠금 없음), 쓰기는 MOD-12 뮤텍스·MOD-09 큐로만.
- 표 단일 원본: 레지스터 표를 수작업으로 편집하지 않는다(MOD-20 생성물만 사용, NFR-022 일치).

## 5. 완료 기준 (각 항목은 단위 테스트로 검증 가능해야 함)
| # | 기준 | 검증 방법 |
|---|---|---|
| 1 | regmap 전 항목 읽기/쓰기(속성대로) 성공, 미정의 주소 예외 02 | 호스트 단위 테스트(생성 표 기반 자동 케이스) |
| 2 | 32비트 값 하위 워드 먼저, 반쪽 쓰기 예외 03(신규 영역). 레거시 창은 기존 동작대로 단일 워드 쓰기(FC06) 허용 후 32비트 범위 검증[E2] | 호스트 단위 테스트 |
| 2a | 레거시 창 응답이 servo1axis `process_request`와 같음(같은 요청 열에 대한 응답 바이트 비교, 기존 코드는 호스트 빌드용으로 복사해 기준 출력 생성) | 호스트 비교 시험 |
| 3 | 범위 밖 값 예외 03, 요청 전체 미반영 | 호스트 단위 테스트 |
| 4 | 1축 모델에서 축2 영역 예외 02 | 호스트 단위 테스트 |
| 5 | FC 01/03/04/05/06/15/16 형식 적합, 미지원 FC 예외 01 | 호스트 단위 테스트 + 제3자 Modbus 시험 도구(STD-003) |
| 6 | 축 명령 레지스터 → 동작 → cmd_result 코드 | HIL(FR-004, FR-023(b)) |

## 6. 미확인·가정
- 지원 FC 목록, 읽기 전용 주소 쓰기 예외 02 [가정](DC-15). 워드 순서 하위 먼저는 Weintek 실적[E6] 기반, M2I 설정 가능 여부 미확인(PS-29).
- 레거시 창의 반쪽 쓰기 허용은 기존 HMI 호환을 위한 예외 규칙이다(신규 영역과 규칙이 다름 — 문서에 명시).
- Modbus 규격 원문(PDU 최대 길이, 개수 상한) 대조 전.
