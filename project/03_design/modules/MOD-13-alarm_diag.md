# 설계→코딩 인터페이스: alarm_diag (알람·정지 원인·이력·진단 계수)

| 모듈 ID | 버전 | 작성 | 대상 언어/플랫폼 | 추적 SRS ID |
|---|---|---|---|---|
| MOD-13 | 0.1 | architect | C(MCU 무관) | FR-011(a), FR-021, FR-023(d), NFR-013 |

## 1. 책임 (이 모듈이 하는 일 / 하지 않는 일)
- 한다: 알람 코드 표(interface-spec §5.8) 관리, 축·시스템별 현재 알람 설정·해제, 마지막 정지 원인 기록, 알람 이력 링 버퍼(HIST_N = 32 [가정], 최신순, 코드·축·가동 시간), 이력의 비휘발 기록(빈도 제한), 진단 계수(알람 누계, 언더런 누계, 워치독 리셋 이력, 통신 오류 수).
- 하지 않는다: 정지 동작 자체(MOD-07·09·10), 표시(MOD-23).

## 2. 의존성
- 사용: MOD-06(ALARM_LOG 영역), MOD-01(시간), MOD-07(stats 읽기).
- 사용하는 쪽: MOD-09, MOD-10, MOD-12, MOD-14(레지스터 0x3000~, B+0x200~), MOD-19(리셋 원인 기록).

## 3. 입출력 정의
| 구분 | 이름 | 타입/단위 | 범위 | 설명 |
|---|---|---|---|---|
| 입력 | raise(axis, code) | uint8_t, uint16_t | axis 0 = 시스템 | ISR에서는 링 버퍼 경유 |
| 입력 | clear(axis) | | | ALARM_RESET 처리 결과로 MOD-09가 호출 |
| 출력 | current_alarm(axis) | U16 | | |
| 출력 | history[i] | {code, axis, uptime_s} | i = 0 최신 | |
| 출력 | counters | 구조체 | | |
| 오류 | — | | 이력 기록 실패 시 E-NVM 경고 | |

공개 API(시그니처 수준):
```c
void     ad_init(void);                                /* loads history from NVM */
void     ad_raise(uint8_t axis, uint16_t code);        /* task context */
void     ad_raise_from_isr(uint8_t axis, uint16_t code);
void     ad_clear(uint8_t axis);
uint16_t ad_current(uint8_t axis);
void     ad_set_stop_cause(uint8_t axis, uint16_t cause);
uint16_t ad_stop_cause(uint8_t axis);
uint16_t ad_history(ad_entry_t *out, uint16_t max);    /* newest first */
void     ad_clear_history(void);
void     ad_service_tick(void);                        /* SERVICE task: flush to NVM (rate-limited) */
void     ad_get_counters(ad_counters_t *out);
```

## 4. 제약 조건
- 이력 기록은 발생 즉시 RAM, NVM 기록은 SERVICE 태스크에서 묶어서(최소 간격 5 s [가정]) → 플래시 수명·T-04 간섭 제한. 전원 차단 직전 이력 일부 유실 가능성은 사용자 문서에 명시 [가정](FRAM 채택 시 즉시 기록).
- 같은 알람의 연속 발생은 1건으로 묶음(채터링 억제, 1 s [가정]).
- 시각 기준은 가동 시간(RTC 없음 [가정]). 실시간 시각 표시가 필요하면 HMI/PC 측 시각 부여(v1 범위 밖 [가정]).

## 5. 완료 기준 (각 항목은 단위 테스트로 검증 가능해야 함)
| # | 기준 | 검증 방법 |
|---|---|---|
| 1 | 이력 33건 발생 시 최신 32건이 최신순 유지 | 호스트 단위 테스트 |
| 2 | 전원 재투입 후 이력 보존(마지막 플러시 이전 건) | HIL |
| 3 | 정지 원인 코드가 원인별 재현 시 정확히 기록 | HIL(FR-021) |
| 4 | 채터링 억제 동작 | 호스트 단위 테스트 |
| 5 | 워치독 리셋 후 E-WDT 이력 1건 | HIL |

## 6. 미확인·가정
- 이력 건수 HIST_N(FR-021 미확인) → 32 [가정].
- RTC 미탑재 [가정].
