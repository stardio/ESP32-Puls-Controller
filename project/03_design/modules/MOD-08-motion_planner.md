# 설계→코딩 인터페이스: motion_planner (가감속 프로파일 계산)

| 모듈 ID | 버전 | 작성 | 대상 언어/플랫폼 | 추적 SRS ID |
|---|---|---|---|---|
| MOD-08 | 0.1 | architect | C(순수 함수, HAL 의존 없음, 호스트·타깃 동일 코드) | FR-006, FR-007, FR-019, FR-020, NFR-004 |

## 1. 책임 (이 모듈이 하는 일 / 하지 않는 일)
- 한다: 이동 1건(절대·상대 위치결정, JOG, 원점복귀의 각 단계)에 대해 시작 속도 → 이동 속도 → 감속 → 정지의 속도 프로파일을 만들고, 호출마다 "다음 구간의 정수 펄스 수와 주파수"를 낸다. 사다리꼴·S자(근사) 곡선, 짧은 거리의 삼각형 프로파일, 운전 중 감속 정지 요청, JOG(무한 거리, 정지 요청 시 감속)를 지원한다.
- 보장: 구간 펄스 수 합 = 이동 거리(정수, 오차 0), 주파수 ≤ `v_max`, 목표를 넘어가지 않음(overshoot 0).
- 하지 않는다: 하드웨어 접근, 위치 보관(MOD-07), 소프트 리밋 판정(MOD-09가 목표값을 검사하고, JOG는 MOD-09가 리밋 위치까지의 거리를 move.distance로 준다).

## 2. 의존성
- 사용: 없음(표준 C 정수 연산만).
- 사용하는 쪽: MOD-07(L1 ISR에서 `mp_next` 호출), MOD-09(이동 거리 산정·사전 검사), MOD-22(호스트 시험).

## 3. 입출력 정의
| 구분 | 이름 | 타입/단위 | 범위 | 설명 |
|---|---|---|---|---|
| 입력 | move.distance | uint32_t, pulse | 0~2^31−1, JOG는 MP_INFINITE | 이동량(방향은 MOD-07이 관리) |
| 입력 | move.v_start | uint32_t, pps | 1~v_max | 기동 속도(`start_speed`) |
| 입력 | move.v_max | uint32_t, pps | ≥ v_start | 이동 속도 |
| 입력 | move.accel_rate / decel_rate | uint32_t, pps/s | > 0 | MOD-09가 `max_speed ÷ accel_time_ms`로 환산 [가정, DC-15] |
| 입력 | move.curve, s_ratio | enum, % | TRAPEZOID / S_CURVE, 0~100 | FR-007(곡선 종류 미확인) |
| 입력 | seg_target_us | uint32_t, µs | 100~10,000 | 구간 목표 길이 |
| 출력 | mp_seg_t.count | uint32_t, pulse | 0 = 종료 | 다음 구간 펄스 수 |
| 출력 | mp_seg_t.freq_pps | uint32_t, pps | v_start~v_max | 구간 주파수 |
| 오류 | mp_err_t | | MP_EINVAL(속도·가속 0, v_start > v_max) | |

공개 API(시그니처 수준):
```c
typedef enum { MP_TRAPEZOID = 0, MP_S_CURVE = 1 } mp_curve_t;
#define MP_INFINITE 0xFFFFFFFFu
typedef struct { uint32_t distance, v_start, v_max, accel_rate, decel_rate; mp_curve_t curve; uint8_t s_ratio; } mp_move_t;
typedef struct { uint32_t count; uint32_t freq_pps; } mp_seg_t;
typedef struct { /* opaque state: phase, remaining, v_cur (fixed-point), decel distance, ... */ } mp_t;

mp_err_t mp_init(mp_t *p, const mp_move_t *m);
mp_seg_t mp_next(mp_t *p, uint32_t seg_target_us);          /* O(1), no division by variable in hot path if avoidable */
void     mp_request_stop(mp_t *p, uint32_t decel_rate);       /* decelerate to stop from current speed */
uint32_t mp_remaining(const mp_t *p);
uint32_t mp_stop_distance(uint32_t v_cur, uint32_t v_start, uint32_t decel_rate);
```

알고리즘(의사코드):
```
mp_next(p, T):
    v = p.v_cur
    d_stop = stop_distance(v, v_start, decel)          /* pulses needed to reach v_start */
    if p.remaining <= d_stop or p.stop_requested: phase = DECEL
    elif v < v_max: phase = ACCEL else: phase = CRUISE
    v_next = update(v, phase, T)                        /* trapezoid: v +/- a*T ; S-curve: jerk-limited ramp in accel window */
    n = (v_next * T + p.frac) / 1e6 ; p.frac = remainder  /* fixed-point accumulator, no drift */
    n = max(n, 1) ; n = min(n, p.remaining)              /* never overshoot */
    if phase == DECEL and v_next <= v_start: n = ... so that tail finishes at v_start
    p.remaining -= n ; p.v_cur = v_next
    return { n, v_next }   /* count 0 when remaining == 0 */
```

## 4. 제약 조건
- 실행 시간: `mp_next`는 L1 ISR에서 호출되므로 상한이 있는 상수 시간이어야 한다(반복문 없음, 64비트 정수 곱셈 허용). 목표 ≤ 10 µs [가정](C1 168 MHz 기준 측정).
- 연산: 정수·고정소수점만 [가정](MOD-07 §4). 호스트와 타깃 결과가 비트 단위로 같아야 한다.
- 스레드 안전: 인스턴스별 상태, 공유 상태 없음. 같은 인스턴스를 두 문맥에서 동시에 호출하지 않는다(MOD-07이 보장).
- 메모리: 인스턴스당 수십 바이트, 동적 할당 없음.

## 5. 완료 기준 (각 항목은 단위 테스트로 검증 가능해야 함)
| # | 기준 | 검증 방법 |
|---|---|---|
| 1 | 무작위 이동 10만 건: Σcount = distance(오차 0), 최대 freq ≤ v_max | 호스트 단위 테스트 |
| 2 | 사다리꼴: 가속 구간 속도 기울기 = accel_rate ± 양자화 오차(허용치 ④에서 정의) | 호스트 단위 테스트 |
| 3 | 짧은 거리(가속 거리 합 > distance)에서 삼각형 프로파일, overshoot 0 | 호스트 단위 테스트 |
| 4 | 운전 중 `mp_request_stop` → 감속 정지 거리 = `mp_stop_distance` 계산값 | 호스트 단위 테스트 |
| 5 | JOG(MP_INFINITE) + 정지 요청 → 감속 후 count 0 | 호스트 단위 테스트 |
| 6 | 타깃에서 `mp_next` 최악 실행 시간 측정값 기록 | HIL |

## 6. 미확인·가정
- 가감속 곡선 종류(FR-007): 사다리꼴 필수, S자는 [가정] 포함. G3 확인.
- 가감속 입력 단위 = "0 → max_speed 도달 시간(ms)" [가정, DC-15].
- 감속 정지 요청 중 목표 변경(운전 중 목표 갱신)은 v1 미지원 [가정].
