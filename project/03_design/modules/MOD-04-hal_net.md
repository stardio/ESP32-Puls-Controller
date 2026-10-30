# 설계→코딩 인터페이스: hal_net (이더넷·TCP HAL)

| 모듈 ID | 버전 | 작성 | 대상 언어/플랫폼 | 추적 SRS ID |
|---|---|---|---|---|
| MOD-04 | 0.1 | architect | C — port_stm32f4(ETH MAC + LAN8720A + lwIP [가정]) / port_esp32(EMAC + esp_netif/lwIP) | FR-022, NFR-003, NFR-010, NFR-023 |

## 1. 책임 (이 모듈이 하는 일 / 하지 않는 일)
- 한다: PHY·MAC 초기화, 링크 상태, 고정 IP 설정(IP·마스크·게이트웨이), MAC 주소 제공, TCP 서버 소켓(listen/accept/recv/send/close)의 MCU 무관 래퍼.
- 하지 않는다: Modbus 처리(MOD-15), 설정 저장(MOD-12). DHCP는 v1 미지원 [가정].

## 2. 의존성
- 사용: MOD-01, lwIP(두 포트 공통 [가정]), PHY 드라이버(LAN8720A).
- 사용하는 쪽: MOD-15 modbus_tcp, MOD-17 fw_update(Modbus 경유이므로 간접).

## 3. 입출력 정의
| 구분 | 이름 | 타입/단위 | 범위 | 설명 |
|---|---|---|---|---|
| 입력 | cfg.ip / mask / gw | uint32_t | IPv4 | 부팅 시 MOD-12에서 받음 |
| 입력 | port, backlog | uint16_t, int | 502 [가정], max_clients | |
| 출력 | link_up | bool | | 링크 상태 |
| 출력 | sock | int | ≥ 0 | 소켓 핸들 |
| 출력 | recv/send 반환 | int, byte | ≥ 0 바이트, < 0 오류 | |
| 오류 | hal_err_t / 음수 반환 | | HAL_ETIMEOUT, HAL_EIO(연결 끊김) | |

공개 API(시그니처 수준):
```c
typedef struct { uint32_t ip, mask, gw; uint8_t mac[6]; } hal_net_cfg_t;
hal_err_t hal_net_init(const hal_net_cfg_t *cfg);
bool      hal_net_link_up(void);
void      hal_net_get_mac(uint8_t mac[6]);
int       hal_tcp_listen(uint16_t port, int backlog);
int       hal_tcp_accept(int lsock, uint32_t timeout_ms);              /* returns sock or <0 */
int       hal_tcp_recv(int sock, uint8_t *buf, size_t len, uint32_t timeout_ms);
int       hal_tcp_send(int sock, const uint8_t *buf, size_t len);
void      hal_tcp_close(int sock);
hal_err_t hal_tcp_set_keepalive(int sock, uint32_t idle_ms);
```

## 4. 제약 조건
- 이더넷 인터럽트·DMA 우선순위는 펄스 엔진(L1)보다 낮게 둔다. 수신 폭주(브로드캐스트 등) 때도 L1 마감을 지켜야 한다 → T-03에서 브로드캐스트 부하 포함 시험 [가정].
- lwIP 메모리 풀 크기는 `max_clients`(기본 4 [가정]) 기준으로 정적 할당.
- 스레드 안전: 소켓 API는 COMM 등급 태스크에서만 호출.
- MAC 주소 출처(구매 OUI, MAC 내장 EEPROM, MCU 고유 ID 파생)는 미정(interface-spec 이슈 #4) → `hal_net_cfg_t.mac`으로 주입.

## 5. 완료 기준 (각 항목은 단위 테스트로 검증 가능해야 함)
| # | 기준 | 검증 방법 |
|---|---|---|
| 1 | 링크 업, 설정 IP로 ping·TCP 접속 성공 | HIL |
| 2 | `max_clients`개 동시 접속, 초과 접속은 거부 또는 가장 오래된 유휴 연결 정리(정책은 MOD-15) | HIL(MOD-22 부하 도구) |
| 3 | 케이블 분리 → 링크 다운 감지, 재연결 후 서버 복구 | HIL |
| 4 | 최대 부하 중 펄스 엔진 언더런 0 | HIL T-03 |
| 5 | STM32F407 + LAN8720A 조합 링크·처리량 기록 | HIL(R-11) |

## 6. 미확인·가정
- RMII 핀은 2차 출처만 있음(interface-spec §2.2). LAN8720A 기준 클록 공급 방식 미확인.
- lwIP 공통 사용, DHCP 미지원 [가정].
- 포트 수 1 [가정](DC-03, Q26).
