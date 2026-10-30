# 설계→코딩 인터페이스: modbus_tcp (Modbus TCP 서버·연결 감시)

| 모듈 ID | 버전 | 작성 | 대상 언어/플랫폼 | 추적 SRS ID |
|---|---|---|---|---|
| MOD-15 | 0.1 | architect | C(MCU 무관, MOD-04 위) | FR-005, FR-012, FR-019, FR-022, FR-023, NFR-003, NFR-010, NFR-023, STD-003, CON-012 |

## 1. 책임 (이 모듈이 하는 일 / 하지 않는 일)
- 한다: 포트 502 [가정] TCP 서버. 최대 `max_clients`(기본 4 [가정]) 동시 연결. MBAP 헤더(트랜잭션 ID, 프로토콜 ID = 0, 길이, 유닛 ID) 검사·응답. 연결별 마지막 요청 시각 기록. 통신두절 감시(FR-012: `commloss_enable`, `commloss_time_ms`, `commloss_scope`)와 JOG 유지 감시(`jog_keepalive_ms`) 결과를 MOD-09에 통지. 접속 수를 진단 레지스터로 제공. 유휴 연결 정리.
- 하지 않는다: PDU 해석(MOD-14), 소켓 하드웨어(MOD-04).

## 2. 의존성
- 사용: MOD-04, MOD-14, MOD-09(`ax_notify_conn`, `ax_stop_all`), MOD-12(설정), MOD-13(E-COMMLOSS).
- 사용하는 쪽: MOD-19(태스크 생성).

## 3. 입출력 정의
| 구분 | 이름 | 타입/단위 | 범위 | 설명 |
|---|---|---|---|---|
| 입력 | TCP 스트림 | byte | | ADU = MBAP(7 B) + PDU |
| 입력 | max_clients | U16 | 1~미확인 | NFR-023 |
| 입력 | commloss 설정 | | interface-spec §5.3 | |
| 출력 | 응답 ADU | byte | | 트랜잭션 ID 그대로 반환 |
| 출력 | conn 이벤트 | (conn_id, alive/lost) | | MOD-09 |
| 출력 | tcp_clients | U16 | | 0x0007 |
| 오류 | 잘못된 MBAP | | 프로토콜 ID ≠ 0, 길이 불일치 → 연결 종료 [가정] | |

공개 API(시그니처 수준):
```c
hal_err_t mb_tcp_start(uint16_t port, uint8_t max_clients);   /* creates COMM task(s) */
uint8_t   mb_tcp_client_count(void);
void      mb_tcp_service_tick(void);                          /* 10 ms [assumed]: timeouts, comm-loss eval */
```

동작(의사코드):
```
server task: select/poll over listen + client sockets (single task [assumed])
  on accept: if count == max -> close oldest idle conn (policy [assumed]) or refuse
  on data: reassemble ADU by MBAP length; unit_id must match param or 0xFF [assumed]
           rsp = mb_core_handle(ctx{conn_id}); send
           last_rx[conn] = now
  on close/error: ax_notify_conn(conn, lost)
tick: for each conn: if now - last_rx > jog_keepalive_ms: ax_notify_conn(conn, lost_keepalive)
      if commloss_enable and (scope==ALL ? all conns silent > commloss_time_ms : commanding conn silent): ax_stop_all(E-COMMLOSS)
```

## 4. 제약 조건
- 태스크 등급 COMM(MOTION보다 낮음). 최대 부하(최대 연결 × 연속 요청)에서 펄스 엔진 언더런 0(T-03).
- 응답 시간 측정값 공개(NFR-010 값 미확인).
- "연결 없음"이 처음부터 이어지는 경우(HMI 미사용 설비)는 두절로 보지 않는다 [가정](최초 연결 이후부터 감시).
- 유닛 ID 처리 규칙 [가정]: 설정값 또는 0xFF 수락.

## 5. 완료 기준 (각 항목은 단위 테스트로 검증 가능해야 함)
| # | 기준 | 검증 방법 |
|---|---|---|
| 1 | 제3자 Modbus TCP 클라이언트로 FR-004 전 항목 읽기/쓰기 | HIL(FR-022) |
| 2 | `max_clients`개 동시 접속에서 각 연결 응답, 응답 시간 분포 기록 | HIL(MOD-22 부하 도구, NFR-023·010) |
| 3 | 분할 수신(ADU가 여러 TCP 세그먼트) 재조립 | 호스트 단위 테스트(가짜 소켓) |
| 4 | 케이블 분리 → `commloss_time_ms` 후 감속 정지(사용 시), 미사용 시 운전 계속 | HIL(FR-012) |
| 5 | JOG 중 해당 연결 무요청 `jog_keepalive_ms` → 감속 정지 | HIL |
| 6 | 최대 부하 중 underrun 0 | HIL T-03 |

## 6. 미확인·가정
- 포트 502, 기본 동시 접속 4, 유닛 ID 규칙, 초과 접속 정책 [가정]. NFR-023 값 미확인(Q26).
- 단일 태스크 select 구조 [가정] — 연결별 태스크 대비 메모리 절약.
