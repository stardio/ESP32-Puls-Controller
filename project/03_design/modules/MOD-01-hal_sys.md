# 설계→코딩 인터페이스: hal_sys (시스템 HAL·OSAL)

| 모듈 ID | 버전 | 작성 | 대상 언어/플랫폼 | 추적 SRS ID |
|---|---|---|---|---|
| MOD-01 | 0.1 | architect | C(STM32 HAL·LL + FreeRTOS [가정]) / C(ESP-IDF) — 포트 구현 교체 | NFR-002, NFR-003, NFR-013, NFR-014, CON-001, CON-008 |

## 1. 책임 (이 모듈이 하는 일 / 하지 않는 일)
- 한다: 클록·시스템 초기화, 단조 증가 시간(µs·ms), 임계 구역, OSAL(태스크·큐·뮤텍스·지연·이벤트 플래그), 워치독, 리셋 원인, 재부팅, 고유 ID 읽기, "빠른 실행 영역" 배치 매크로(L1 코드·데이터를 RAM에 두기 위함, architecture R-13).
- 하지 않는다: 주변장치(펄스·I/O·네트워크·시리얼·NVM) 제어 — MOD-02~06 몫. 태스크 구성 결정 — MOD-19 몫.
- 상위 모듈은 RTOS·벤더 API를 직접 호출하지 않고 이 모듈만 쓴다(DC-14).

## 2. 의존성
- 사용: 포트별 벤더 SDK(STM32 HAL·LL 또는 ESP-IDF), FreeRTOS [가정].
- 사용하는 쪽: 모든 펌웨어 모듈(MOD-02~19).

## 3. 입출력 정의
| 구분 | 이름 | 타입/단위 | 범위 | 설명 |
|---|---|---|---|---|
| 입력 | wdt_timeout_ms | uint32_t, ms | 미확인(포트별 하드웨어 한도) | 워치독 시간 |
| 입력 | task 속성 | 우선순위 등급, 스택 바이트 | OSAL_PRIO_MOTION > COMM > SERVICE | 실행 등급(architecture §3 L2~L4) |
| 출력 | hal_time_us() | uint32_t, µs | 0~2^32−1(약 71.6분 순환) | 차이 계산은 부호 없는 뺄셈으로 순환 안전 |
| 출력 | hal_time_ms() | uint32_t, ms | 순환 | |
| 출력 | reset_cause | enum | POWER_ON, WATCHDOG, SOFTWARE, PIN, BROWNOUT, UNKNOWN | 알람 이력 E-WDT 기록용 |
| 오류 | hal_err_t | 오류 코드 | HAL_OK, HAL_EINVAL, HAL_EBUSY, HAL_ETIMEOUT, HAL_EIO, HAL_ENOTSUP, HAL_ENOMEM | 모든 HAL 공통 오류형 |

공개 API(시그니처 수준):
```c
typedef enum { HAL_OK = 0, HAL_EINVAL, HAL_EBUSY, HAL_ETIMEOUT, HAL_EIO, HAL_ENOTSUP, HAL_ENOMEM, HAL_EUNDERRUN } hal_err_t;

hal_err_t hal_sys_init(void);
uint32_t  hal_time_us(void);
uint32_t  hal_time_ms(void);
void      hal_crit_enter(void);          /* nestable, ISR-safe */
void      hal_crit_exit(void);
hal_err_t hal_wdt_start(uint32_t timeout_ms);
void      hal_wdt_kick(void);
hal_reset_cause_t hal_sys_reset_cause(void);
void      hal_sys_reboot(void);
void      hal_sys_uid(uint8_t *out, size_t len);

/* OSAL (thin wrappers) */
typedef enum { OSAL_PRIO_SERVICE, OSAL_PRIO_COMM, OSAL_PRIO_MOTION } osal_prio_t;
hal_err_t osal_task_create(void (*fn)(void *), const char *name, osal_prio_t prio, size_t stack, void *arg, osal_task_t *out);
hal_err_t osal_queue_create(size_t item_size, size_t depth, osal_queue_t *out);
hal_err_t osal_queue_send(osal_queue_t q, const void *item, uint32_t timeout_ms);
hal_err_t osal_queue_send_from_isr(osal_queue_t q, const void *item);
hal_err_t osal_queue_recv(osal_queue_t q, void *item, uint32_t timeout_ms);
hal_err_t osal_mutex_create(osal_mutex_t *out);
hal_err_t osal_mutex_lock(osal_mutex_t m, uint32_t timeout_ms);
void      osal_mutex_unlock(osal_mutex_t m);
void      osal_delay_ms(uint32_t ms);
void      osal_delay_until_ms(uint32_t *last_wake, uint32_t period_ms);

/* placement macros: map to RAM function/data sections per port */
#define HAL_FAST_CODE   /* e.g. section(".ramfunc") or IRAM_ATTR */
#define HAL_FAST_DATA
```

## 4. 제약 조건
- 타이밍: `hal_time_us` 분해능 ≤ 1 µs, 호출 비용은 L1 ISR에서 써도 되는 수준(수십 사이클 이내 [가정], 포트별 측정).
- ISR: `hal_crit_*`, `hal_time_*`, `osal_queue_send_from_isr`만 ISR에서 호출 가능. STM32 포트에서 RTOS 시스템 호출 우선순위보다 높은 인터럽트(L1)는 OSAL을 호출하면 안 된다 → L1→L2 전달은 잠금 없는 링 버퍼(MOD-07)로 한다.
- 임계 구역 최대 길이: 펄스 엔진 마감을 해치지 않도록 상위 모듈 사용 시 10 µs 이하 [가정].
- 스레드 안전: OSAL 함수는 태스크 간 안전. `hal_crit_*`는 중첩 가능.
- 메모리: 동적 할당은 초기화 단계에서만 허용 [가정](운전 중 malloc 금지).

## 5. 완료 기준 (각 항목은 단위 테스트로 검증 가능해야 함)
| # | 기준 | 검증 방법 |
|---|---|---|
| 1 | `hal_time_us` 단조 증가, 순환 시 차이 계산 정상 | 호스트 시뮬(순환 경계 값) + HIL(1 s 측정 대 외부 기준 오차 기록) |
| 2 | 워치독 미갱신 시 리셋, 재기동 후 `reset_cause == WATCHDOG` | HIL |
| 3 | OSAL 큐 송수신·시간 초과 동작 | 호스트 시뮬(POSIX 포트 [가정]) + HIL |
| 4 | `HAL_FAST_CODE` 함수가 RAM 주소에 배치됨 | 링크 맵 검사 |
| 5 | 두 포트(port_stm32f4, port_esp32)에서 같은 시험 코드가 컴파일·통과 | 빌드 + HIL |

## 6. 미확인·가정
- FreeRTOS 공통 사용 [가정](DC-14). STM32 측 RTOS 라이선스·버전은 ④에서 확정.
- 워치독 시간 한도·리셋 원인 구분 가능 항목은 MCU 데이터시트 미대조(미확인).
- 호스트 시뮬용 POSIX 포트 제공 [가정] — MOD-07·08·09 호스트 시험에 필요.
