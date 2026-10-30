# 설계→코딩 인터페이스: modbus_tcp (Modbus TCP 서버·연결 감시)

| 모듈 ID | 버전 | 작성 | 대상 언어/플랫폼 | 추적 SRS ID |
|---|---|---|---|---|
| MOD-15 | 0.2 | architect | C(MCU 무관, MOD-04 위) | FR-005, FR-012, FR-019, FR-022, FR-023, NFR-003, NFR-010, NFR-023, STD-003, CON-012 |

> v0.2 개정: servo1axis `server_task`·`client_task`·`receive_exact`·`send_exact`[E2]를 **수정 후 재사용**한다. 기존은 연결마다 태스크 1개 구조이며 기존 HMI와 운전 실적이 있으므로 v0.1의 "단일 select 태스크 [가정]"을 버리고 연결별 태스크 구조를 유지한다.

## 1. 책임 (이 모듈이 하는 일 / 하지 않는 일)
- 한다: 포트 502 TCP 서버, 연결별 COMM 태스크, MBAP 검사(프로토콜 ID 0, 길이 2~254)·응답(트랜잭션 ID·유닛 ID 반환), 유닛 ID 검사, `max_clients` 상한, 연결별 마지막 요청 시각, 통신두절·JOG 유지 감시 통지, 접속 수 제공.
- 하지 않는다: PDU 해석(MOD-14), 소켓(MOD-04).

## 2. 의존성
- 사용: MOD-04, MOD-14, MOD-09(`ax_notify_conn`, `ax_stop_all`), MOD-12, MOD-13.
- 사용하는 쪽: MOD-19.
- 재사용[E2]: `receive_exact`/`send_exact`(그대로), MBAP 헤더 처리(유닛 ID 불일치 시 예외 0x0B 응답 — 기존 동작), 수신·송신 타임아웃 5 s, 연결 종료 시 접속 수 감소. 수정: 접속 수 상한, 유닛 ID 규칙, 연결 ID 부여, 감시 통지 추가, 태스크 스택·우선순위 상수화.

## 3. 입출력 정의
| 구분 | 이름 | 타입/단위 | 범위 | 설명 |
|---|---|---|---|---|
| 입력 | TCP 스트림 | byte | | ADU = MBAP(7 B) + PDU(≤ 253 B) |
| 입력 | max_clients | U16 | 1~미확인, 기본 4 [가정] | 기존 backlog 4[E2] |
| 입력 | unit_id | U8 | 1~247, 기본 1[E5] | 0x4006 |
| 입력 | commloss 설정 | | interface-spec §5.3 | |
| 출력 | 응답 ADU | byte | | |
| 출력 | conn 이벤트 | (conn_id, alive/lost) | | MOD-09 |
| 출력 | tcp_clients | U16 | | 0x0027, 레거시 HR 0x000F |
| 오류 | 잘못된 MBAP | | 프로토콜 ID ≠ 0, 길이 범위 밖 → 연결 종료(기존 동작) | |

공개 API(시그니처 수준):
```c
hal_err_t mb_tcp_start(uint16_t port, uint8_t max_clients, uint8_t unit_id);
uint8_t   mb_tcp_client_count(void);
void      mb_tcp_service_tick(void);   /* 10 ms [assumed]: keepalive & comm-loss evaluation */
```

동작(의사코드):
```
server task: listen(port, backlog = max_clients); loop accept
  if count >= max_clients: close new socket (policy: refuse newest [assumed])
  else spawn client task(conn_id) prio COMM, stack 4 KB (existing values)
client task: loop
  receive_exact(7) ; check protocol==0, 2 <= len <= 254 ; receive_exact(len-1)
  unit = hdr[6]; if unit != unit_id and unit != 0xFF [assumed]: exception 0x0B (existing)
  rsp = mb_core_handle(ctx{conn_id, TCP, unit}); send_exact
  last_rx[conn_id] = now
  on error/close: ax_notify_conn(conn_id, lost); count--
tick: keepalive (jog_keepalive_ms) and comm-loss rules (unchanged from v0.1)
```

## 4. 제약 조건
- COMM 태스크 우선순위 < MOTION. 기존 값(서버 5, 연결 6, 운동 7)[E2]은 상대 순서만 유지하고 L1은 ISR/하드웨어 타이머로 분리했으므로 운동 태스크와의 관계는 T-05에서 재확인.
- 최대 부하(4연결 × 연속 요청)에서 펄스 오차 0(T-05).
- 응답 시간 분포 공개(NFR-010 미확인).
- 최초 연결 전 무연결은 두절로 보지 않는다 [가정].

## 5. 완료 기준 (각 항목은 단위 테스트로 검증 가능해야 함)
| # | 기준 | 검증 방법 |
|---|---|---|
| 1 | 기존 MT8051iP 프로젝트로 레거시 창 전 항목 동작(무수정) | HIL(시연) |
| 2 | 제3자 클라이언트로 FR-004 전 항목 | HIL |
| 3 | `max_clients`개 동시 접속 응답, 초과 접속 거부, 응답 시간 분포 기록 | HIL(MOD-22) |
| 4 | 분할 수신 재조립(`receive_exact`) | 호스트 단위 테스트(가짜 소켓) |
| 5 | 통신두절·JOG 유지 정지 | HIL |
| 6 | 최대 부하 중 펄스 오차 0 | HIL(T-05) |

## 6. 미확인·가정
- 포트 502, 기본 동시 접속 4, 유닛 ID 0xFF 수락, 초과 접속 정책 [가정]. NFR-023 값 미확인.
- 연결별 태스크 메모리(4 KB × 4)의 여유는 ESP32-P4 메모리상 문제없다고 본다 [가정](보드 PSRAM 32 MB[S14], 내부 SRAM 크기 미대조).
