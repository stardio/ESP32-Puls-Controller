# 설계→코딩 인터페이스: hal_net (이더넷·TCP HAL)

| 모듈 ID | 버전 | 작성 | 대상 언어/플랫폼 | 추적 SRS ID |
|---|---|---|---|---|
| MOD-04 | 0.2 | architect | C — **port_esp32p4(내장 EMAC + IP101, esp_netif/lwIP)** 1차 / port_stm32f4(ETH MAC + LAN8720A)는 전환 시만 | FR-022, NFR-003, NFR-010, NFR-023 |

> v0.2 개정: 기준선을 ESP32-P4 내장 EMAC + 온보드 IP101로 바꿨다(D-024, D-027). servo1axis `movion_modbus_network_start`의 EMAC·IP101 초기화 부분[E2]을 **수정 후 재사용**한다. W5500 2차 인터페이스는 제품에서 제외한다.

## 1. 책임 (이 모듈이 하는 일 / 하지 않는 일)
- 한다: EMAC·IP101 초기화(PHY 주소 1, PHY 리셋 GPIO51[E2]), 링크 이벤트, 고정 IPv4 설정(DHCP 클라이언트 정지), MAC 주소 제공, TCP 소켓 래퍼(listen/accept/recv/send/close).
- 하지 않는다: Modbus 처리(MOD-15), 설정 저장(MOD-12), 2차 이더넷(W5500, 제외). DHCP 미지원 [가정].

## 2. 의존성
- 사용: MOD-01, ESP-IDF `esp_eth`(`esp_eth_mac_new_esp32`, `esp_eth_phy_new_ip101`), `esp_netif`, lwIP 소켓.
- 사용하는 쪽: MOD-15 modbus_tcp.
- 재사용 대상[E2]: EMAC/PHY 생성·드라이버 설치·netif 연결 순서, `esp_netif_dhcpc_stop` + `esp_netif_set_ip_info`, ETH_EVENT 링크 처리. 제거: `start_w5500_interface`, `s_w5500_*`, 링크 OR 처리.

## 3. 입출력 정의
| 구분 | 이름 | 타입/단위 | 범위 | 설명 |
|---|---|---|---|---|
| 입력 | cfg.ip / mask / gw | uint32_t | IPv4 | MOD-12에서 받음. 기본 192.168.0.220/24[E5] [가정: 출하 기본값 DC-03] |
| 입력 | cfg.phy_addr, phy_rst_gpio | int | 1, 51 | 보드 고정값[E2] |
| 입력 | port, backlog | uint16_t, int | 502, max_clients | |
| 출력 | link_up | bool | | |
| 출력 | sock, recv/send 반환 | int | | |
| 오류 | hal_err_t / 음수 | | HAL_ETIMEOUT, HAL_EIO | |

공개 API(시그니처 수준):
```c
typedef struct { uint32_t ip, mask, gw; uint8_t mac[6]; int phy_addr; int phy_rst_gpio; } hal_net_cfg_t;
hal_err_t hal_net_init(const hal_net_cfg_t *cfg);
bool      hal_net_link_up(void);
void      hal_net_get_mac(uint8_t mac[6]);
int       hal_tcp_listen(uint16_t port, int backlog);
int       hal_tcp_accept(int lsock, uint32_t timeout_ms);
int       hal_tcp_recv(int sock, uint8_t *buf, size_t len, uint32_t timeout_ms);
int       hal_tcp_send(int sock, const uint8_t *buf, size_t len);
void      hal_tcp_close(int sock);
hal_err_t hal_tcp_set_keepalive(int sock, uint32_t idle_ms);
```

## 4. 제약 조건
- 이더넷 ISR·lwIP 태스크는 L1(MOD-02·07 ISR)보다 낮은 우선순위. 코어 배치(lwIP·EMAC 태스크 코어 0, L1 코어 1 등)는 [가정]이며 T-05에서 비교(architecture R-19).
- lwIP 메모리 풀은 `max_clients`(기본 4 [가정]) 기준.
- MAC 주소: 기존은 ESP-IDF 기본(efuse 기반 [가정], 코드상 `ETH_MAC_DEFAULT_CONFIG`)[E2]. 출처 정책은 interface-spec 이슈 #4.
- 스레드 안전: 소켓 API는 COMM 태스크에서만.

## 5. 완료 기준 (각 항목은 단위 테스트로 검증 가능해야 함)
| # | 기준 | 검증 방법 |
|---|---|---|
| 1 | 링크 업, 설정 IP로 ping·TCP 접속 | HIL |
| 2 | `max_clients`개 동시 접속 | HIL(MOD-22) |
| 3 | 케이블 분리 → 링크 다운 감지, 재연결 후 복구 | HIL |
| 4 | 최대 부하 중 펄스 오차 0, 갱신 지연 0 | HIL(T-05) |
| 5 | W5500 코드 제거 후에도 기존 HMI(MT8051iP, 192.168.0.221)와 접속 | HIL |

## 6. 미확인·가정
- RMII 핀 번호(보드 내부), IP101 품번·동작 온도 미확인(PS-24, PS-27).
- 1포트 [가정](DC-03, Q26). 2포트가 필요해지면 W5500 코드[E2] 재도입 검토.
- 코어 배치 [가정].
