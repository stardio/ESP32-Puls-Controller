# 원가 절감안 기술 타당성 검토 — 절감안 A(ESP32-S3 + W5500), 절감안 B(TCP/RTU 옵션 분리)

| 문서 | 버전 | 작성 | 작성일 | 기준 입력 |
|---|---|---|---|---|
| CRR-001 | 0.1 | architect | 2026-10-30 | decisions.md D-010·D-015·D-017·D-019·D-024·D-025·D-028·D-029·D-030·D-031·D-032, status.md I-043·I-044, gates/G2-requirements.md(결정: 승인), architecture.md ARCH-001 v0.3.1, bom-draft.md BOM-001 v0.2, parts-comparison.md PARTS-001 v0.2(P25), srs.md v0.7, project/04_code/test-fw/(시험 펌웨어), servo1axis(커밋 2135568, 읽기 전용) |

> 선행 게이트: G2 = **승인**. G3 미승인. 이 문서는 검토 결과와 결정 후보만 담는다. architecture.md·interface-spec.md·SRS는 고치지 않았다(§8에 채택 시 개정 범위를 적음).
> 표기: [Rx] = §10 외부 출처, [Ex] = servo1axis 파일(architecture §9와 같은 번호), [Tx] = 시험 펌웨어 파일. **미확인** = 근거 없음. **[가정]** = 작업 가정. **[산술]** = 확인값으로 계산한 값.
> 출처 한계: 외부 출처는 웹 검색 결과 요약으로 확인했다. 데이터시트·TRM 원문 PDF는 열지 못했다. 원문 대조가 필요한 값은 "원문 대조 필요"로 표시했다.
> 가격: architect는 가격을 만들지 않는다. 사용자 추정 절감액(A 약 1만원, B 약 5천원, I-043·I-044)은 검증하지 않았고, parts-selector 재조사(PS-30·31, §7)로 확인한다.

---

## 0. 요약

| 항목 | 판단 |
|---|---|
| 절감안 A (S3 + W5500) | **기술적으로 조건부 타당.** 2축 펄스 자원(MCPWM 2그룹, PCNT 4유닛)은 문서로 확인했고, 펄스는 하드웨어가 생성하므로 CPU 클록 차이(400 → 240 MHz)는 결정 요인이 아니다. 다만 1 MHz 실적(D-028)은 P4에서 얻은 것이라 S3로 넘어오지 않는다. S3는 단축·2축 모두 미실측이 된다. W5500은 ESP-IDF 5.5.3에 드라이버가 포함되어 있고(servo1axis가 이미 사용 [E2][E8]), MACRAW + lwIP 방식이라 W5500 하드웨어 소켓 8개 제한이 Modbus 접속 수를 묶지 않는다. 남은 위험은 SPI 처리량, GPIO ISR 서비스 공유로 인한 간섭, 브로드캐스트 수신 부하이며, 모두 실측해야 한다 |
| 절감안 B (TCP/RTU 분리) | **RTU 전용 SKU(이더넷 미실장)는 권고하지 않는다.** D-017·D-019와 FR-005·016·022·023, CON-002에 걸리므로 SRS 개정과 G2 기준선 변경이 필요하다. HMI 화면 데이터도 2벌이 된다. **"TCP 표준 + RTU 옵션 실장(공용 PCB에서 부품 미실장)"은 권고한다.** HMI는 TCP로 유지되므로 D-017·D-019와 충돌하지 않고, FR-005의 RTU 필수 [가정]만 '옵션'으로 바꾸는 작은 개정이면 된다 |
| A와 B의 관계 | P4 보드(F1)에는 IP101·RJ45가 보드에 붙어 있어 이더넷을 미실장할 수 없다. 그래서 P4 기준에서 B로 얻는 절감은 RS-485 부품뿐이다. 이더넷 미실장 절감은 A(W5500을 캐리어에 실장)를 채택할 때만 생긴다 |
| 권고 | (1) A는 즉시 기준선을 바꾸지 않는다. 시험 펌웨어에 S3 포트를 추가해 P4와 같은 시험(T-03~T-09)과 신규 T-15~T-19를 S3에서 실측하고, PS-30 원가 결과와 함께 G3에서 결정한다(DC-20). 이를 위해 D-030(2축 실측 보류) 해제가 필요하다. (2) B는 "TCP 표준 + RTU 옵션 실장"으로 한다(DC-24) |
| 새 결정 후보 / 실측 항목 | **DC-20~DC-27 8건, T-15~T-20 6건**, parts-selector 재조사 PS-30~PS-32 3건 |

---

## 1. 검토 1 — ESP32-S3에서 2축 동시 ≥ 200 kpps(D-010)와 1축 1 MHz(D-028 수준)

### 1.1 자원 대조 (2축 Step/Dir 기준)

| 자원 | ESP32-S3 확인값 | 출처 | 2축 필요 | ESP32-P4(참고, architecture §4.3) | 판단 |
|---|---|---|---|---|---|
| MCPWM 그룹 | **2** | [R1] 데이터시트("Two MCPWM"), [R3] SoC Capabilities `SOC_MCPWM_GROUPS` = 2 | 2(축당 1그룹, 기존 설계 그대로) | 2 | **가능** |
| 그룹당 타이머 / 오퍼레이터 | **3 / 3** | [R1] 데이터시트 MCPWM 서술("three PWM timers, three PWM operators") | 그룹당 1 / 1 | 미확인 | **가능**(여유 2) |
| 오퍼레이터당 제너레이터 | 미확인(ESP-IDF 매크로 `SOC_MCPWM_GENERATORS_PER_OPERATOR`는 있으나 값 미열람 [R3]) | [R3] | 1 (CW/CCW 선택 시 2) | 미확인 | Step/Dir 가능. CW/CCW는 TFW-02 probe(`probe_caps`가 제너레이터 2개 생성을 시험함 [T2])로 확인 |
| 그룹당 GPIO 고장 입력 | 미확인 | — | 그룹당 1(비상정지 1.5차 경로) | 미확인 | TFW-04 기동 시 `mcpwm_new_gpio_fault`로 확인(P4와 같은 상태) |
| 타이머 주기 레지스터 폭 | 미확인(ESP32 원형은 16비트 [R1 검색 요약], S3 TRM 원문 대조 필요 [R2]) | — | 최저 주파수 결정 | 미확인 | 시험 펌웨어는 65,535틱 [가정]으로 두고 `probe_caps`가 실제 상한을 기록한다 [T1][T2]. 16비트일 때 10 MHz 해상도의 최저 주파수 약 153 Hz [산술] |
| MCPWM 클록원 | 기본 PLL_F160M(160 MHz 고정) | [R4] ESP-IDF Clock Tree(S3) | 해상도 10/20/40 MHz 후보 | PLL_F160M | P4와 같다. 1 MHz = 10 MHz 해상도에서 10틱으로 정확히 나뉜다 [산술] |
| PCNT 유닛 / 유닛당 채널 | **4 / 2** | [R1] 데이터시트("four pulse counter units", 유닛당 독립 채널 2) | 2 / 1 | 복수 / 미확인 | **가능**(유닛 여유 2) |
| PCNT 계수 범위 | 16비트(1~65,535로 서술) | [R1] 구판 데이터시트 서술(검색 요약) | 상한 30,000 블록 누적 [T1] | — | 시험 펌웨어의 상한 30,000은 16비트 부호 범위 안이다. 그대로 쓸 수 있다 |
| PCNT 감시점 | 미확인(`SOC_PCNT_THRES_POINT_PER_UNIT` 값 미열람) | [R3] | 유닛당 2(상한 + 목표 나머지) | 미확인 | 기존 코드가 상한 감시점과 목표 감시점 2개를 쓴다. S3에서 생성 성공 여부는 TFW-02 기동 시 확인 |
| PCNT 입력 최대 주파수 | 미확인(TRM 원문 미열람) | [R2] | ≥ 1 MHz | 1 MHz 계수 일치 실적(P4) [E5] | **미확인** → T-15에서 1 MHz 계수 오차 0 확인 |
| CPU | Xtensa LX7 2코어, 최대 240 MHz | [R1] | 1 ms 갱신 ISR 2축분 + 통신 | RISC-V 2코어 400 MHz | 펄스 타이밍과 계수는 하드웨어 몫이다. CPU는 1 ms 갱신(정수 연산)과 정지 ISR만 맡는다. 갱신 ISR 소요 시간은 P4에서도 미실측이다(D-030) → T-05·T-09 지표(`upd_max_us`)를 S3에서 측정 |
| ETM(하드웨어 정지 연결) | 미확인(S3 지원 여부 확인 못 함) | — | 선택(P3') | 지원 [S15] | S3에서는 P3'(PCNT → MCPWM 하드웨어 정지)를 쓰지 못할 수 있다. 기준 설계(감시점 ISR + f_stop_max 불변식)는 ETM을 전제하지 않으므로 영향은 선택 실험 1건에 그친다 |
| 듀얼코어 배치 | 2코어 | [R1] | 통신 코어 / 실시간 코어 분리 | 같음 | 시험 펌웨어의 배치 A/B 전환(`TFW_CFG_CORE_B`) [T3]을 그대로 쓴다. W5500 관련 인터럽트 배치는 §2.4 |

### 1.2 GPIO 수

| 항목 | 값 | 근거 |
|---|---|---|
| S3 칩 GPIO | 45개(GPIO0~21, 26~48) | [R1] "45 GPIO". 번호 구간은 [산술](22 + 23 = 45). GPIO22~25는 S3에 없다 |
| 사용 제한 핀 | 내장/외장 플래시·PSRAM용 GPIO26~32, 옥탈 PSRAM 탑재 모듈의 GPIO33~37(제3자 자료는 35~37만 명시), 스트래핑 GPIO0·3·45·46, USB GPIO19·20, 콘솔 UART0 GPIO43·44 | [R12](제3자), [R1] §핀 정의 **원문 대조 필요** |
| 2축 필요 수 | 약 26(architecture §4.3 [산술]) + W5500 4~6(SCK·MOSI·MISO·CS 필수, INT·RST 선택) = **약 30~32** [산술] | architecture §4.3, [E2] |
| 사용 가능 추정 | 옥탈 PSRAM 미탑재 모듈: 45 − 7(26~32) = 38. 옥탈 PSRAM 탑재: 33. 이 중 스트래핑 4개는 출력·풀업 조건을 지키면 쓸 수 있다 [가정] | [산술], 모듈별 노출 핀 수는 **미확인**(PS-30) |
| 판단 | **옥탈 PSRAM 미탑재 모듈이면 가능**(약 38 대 30~32). P4 보드의 잔여 GPIO 27개(판매처 표기) 대비 여유가 늘어나 R-18(2축 GPIO 여유 약 1개)이 완화된다. 옥탈 PSRAM 탑재 모듈이면 여유 1~3개로 P4와 비슷하다. 펌웨어 RAM 요구로 PSRAM이 필요한지는 미확인이나, lwIP + 접속 6~8개 + 2축 상태를 내장 SRAM으로 감당할 수 있다고 본다 [가정] → DC-23 |

- **핀 번호는 전부 다시 정해야 한다.** servo1axis와 시험 펌웨어의 P4 핀 중 PULSE GPIO25, RS-485 TX GPIO24, START GPIO54, PHY 리셋 GPIO51은 S3에 없다. DIR GPIO32, Enable GPIO33, 보조 출력 GPIO26·27은 S3의 플래시·PSRAM 구간이다. hardware.json의 W5500 핀(SCK4·MOSI2·MISO3·CS5·INT6·RST14) [E7]은 S3에도 있지만, GPIO3이 스트래핑 핀이라 MISO로 쓰기에 부적합하다 [R12, 원문 대조 필요]. 또 SPI2 전용(IO MUX) 핀이 아니어서 GPIO 매트릭스를 거친다(§2.3).

### 1.3 판단

| 목표 | 판단 | 이유 |
|---|---|---|
| 2축 동시 축당 ≥ 200 kpps (D-010, NFR-002) | **자원상 가능, 성능 미실측** | MCPWM 그룹 2개와 PCNT 유닛 4개로 축당 1세트를 배정할 수 있다. 200 kHz는 10 MHz 해상도에서 50틱이라 양자화 문제가 없다 [산술]. P4도 2축은 미실측이므로(D-030) 근거 수준은 P4와 같다. 다만 P4에는 단축 실적이 있고 S3에는 없다 |
| 1축 1 MHz (D-028 수준) | **하드웨어상 가능성 높음, 실적 없음** | 출력은 MCPWM이 하드웨어로 만든다. 클록원이 P4와 같은 160 MHz이고(10 MHz 해상도에서 10틱 [산술]), PCNT 블록 누적 ISR 빈도는 1 MHz에서 축당 약 33회/s라 부담이 작다 [산술]. 미확인으로 남는 것은 PCNT 입력 최대 주파수와 주기 레지스터 폭이다. **D-028은 P4 실적이므로 S3 제품에는 "검증 사실"로 표기할 수 없다(D-031).** T-15·T-19로 다시 실측해야 한다 |
| 목표 정지 ISR(f_stop_max 불변식) | 영향 작음 [가정] | 감속 끝 주파수 ≤ 5 kHz [가정]이면 허용 지연은 약 200 µs [산술]다. 240 MHz Xtensa에서도 ISR 지연은 그보다 훨씬 작을 것으로 본다 [가정]. 실제 값은 T-08(S3 빌드)과 T-17(W5500 인터럽트 간섭)로 확인한다 |
| 플래시 쓰기 간섭(R-16) | P4와 같은 위험 | 플래시 작업 중 캐시가 꺼지는 구조는 같다. IRAM 안전 설정 파일 [T4]의 옵션 이름은 ESP-IDF 드라이버 공통 옵션이라 S3에도 적용된다 [가정, 빌드 시 `grep IRAM sdkconfig`로 확인]. 추가로 SPI 마스터 ISR의 IRAM 배치(`CONFIG_SPI_MASTER_ISR_IN_IRAM`) 확인이 필요하다 |

### 1.4 시험 펌웨어(project/04_code/test-fw) S3 이식 범위

시험 펌웨어는 순수 로직(`tfw_logic`: 프로파일·주기 계산·레지스터 창·Modbus PDU, 호스트 시험 가능)과 ESP-IDF 드라이버 사용부가 분리되어 있다. MCPWM·PCNT·GPTimer·GPIO 드라이버 API는 칩과 무관한 ESP-IDF 공통 API다. 이식은 "포트 계층 교체"가 아니라 **설정·핀·이더넷 초기화 교체** 수준이다.

| 파일 | 변경 | 내용 |
|---|---|---|
| sdkconfig.defaults [T5] | 수정 | `CONFIG_IDF_TARGET="esp32s3"`, 플래시 크기(모듈에 맞춤, 32 MB → 미정), `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240`, `CONFIG_ETH_SPI_ETHERNET_W5500=y`, `CONFIG_LWIP_MAX_SOCKETS` 상향(DC-27), 콘솔 설정. 칩 리비전 최소값(P4 v3.1) 제거 |
| sdkconfig.tfw_iram_on [T4] | 확인 + 추가 | 기존 8개 옵션 유지(존재 확인). SPI 마스터 ISR IRAM 옵션 추가 검토 |
| main/Kconfig.projbuild [T3] | 수정 | 축1·축2·비상정지·디버그 GPIO 기본값을 S3 핀맵으로. W5500 핀(SCK·MOSI·MISO·CS·INT·RST)과 SPI 클록, INT/폴링 선택(`poll_period_ms`) 항목 추가 |
| components/tfw_pulse2/tfw_pulse2.c [T2] | 소폭 수정 | MCPWM·PCNT·GPTimer 코드는 그대로 쓴다(`group_id = 축 번호`는 S3 그룹 2개와 맞음). P4 전용 예약 핀 상수 목록(51, 23, 26, 27, 46)을 S3 예약 핀으로 바꾼다. `probe_caps`는 그대로 쓰고 결과로 S3 자원을 기록한다 |
| components/tfw_estop_io/tfw_estop_io.c | 소폭 수정 | API는 같다. 인터럽트 레벨 1~3은 Xtensa C 처리기 범위에서 유효하다 [가정]. **GPIO ISR 서비스를 W5500 INT와 공유하게 되므로 설치 코어·레벨을 다시 정해야 한다**(§2.4, DC-22) |
| components/tfw_modbus/tfw_modbus.c [T6] | **이더넷 초기화 교체** | 내장 EMAC·IP101 초기화(`esp_eth_mac_new_esp32`, `esp_eth_phy_new_ip101`, PHY 리셋 GPIO51)를 SPI 버스 초기화 + `esp_eth_mac_new_w5500`/`esp_eth_phy_new_w5500` + MAC 주소 설정으로 바꾼다. servo1axis의 `start_w5500_interface()` [E2]를 복사해 쓰면 된다(원본 수정 금지). esp_netif 이후 lwIP 소켓 서버(접속 수 상한, 시험 창)는 **변경 없음**. 보조 출력 GPIO 23·26·27·46 상수는 S3 핀으로 바꾼다 |
| main/tfw_main.c | 확인 | 칩 정보·CPU 클록 조회(`esp_chip_info`, `esp_clk_tree`)와 코어 배치는 칩 공통이다. 부팅 로그의 P4 리비전 해석 문구만 정리한다 |
| tools/check_iram.sh | 수정 | `riscv32-esp-elf-nm/objdump` → `xtensa-esp32s3-elf-*`. IRAM 주소 범위 판정값(P4 0x4FFxxxxx [가정])을 S3 값으로 바꾼다(S3 IRAM 주소 범위는 **미확인**, TRM 대조) |
| components/tfw_logic, host/(Python) | 변경 없음 | 기본 IP만 설정에 따라 다르다. 판정·보고 로직은 칩과 무관하다 |
| 빌드 구성 | 추가 | P4와 S3를 나란히 빌드할 수 있게 대상별 sdkconfig(`sdkconfig.defaults.esp32s3`)로 나누는 방식을 권고한다. 그러면 같은 시험을 두 칩에서 비교할 수 있다 |

- 이식은 D-025의 시험 펌웨어 범위를 바꾸는 일이므로 사용자 결정이 필요하다(DC-20). 2축 실측 보류(D-030)도 함께 해제해야 비교가 의미 있다.

---

## 2. 검토 2 — W5500(SPI)으로 Modbus TCP 서버 운용

### 2.1 ESP-IDF 드라이버 지원

| 항목 | 확인 내용 | 출처 |
|---|---|---|
| ESP-IDF 5.5.3(기준선) | `esp_eth` 컴포넌트에 W5500 MAC/PHY 드라이버가 들어 있다(`CONFIG_ETH_SPI_ETHERNET_W5500`). servo1axis가 IDF 5.5.3에서 이 옵션을 켜고 `esp_eth_mac_new_w5500()`과 `ETH_W5500_DEFAULT_CONFIG`를 쓴다 | [E8] sdkconfig, [E2] movion_modbus_tcp.c |
| 동작 방식 | W5500의 소켓 0을 **MAC RAW** 모드로 열어 이더넷 프레임만 주고받고, TCP/IP는 ESP32 쪽 lwIP가 처리한다. 소켓 0 하나에 TX·RX 버퍼 16 KB씩을 모두 준다 | [R6] 드라이버 소스(구 버전 미러, 현행 5.5.3 원문 대조 필요) |
| INT 핀 / 폴링 | `eth_w5500_config_t`에 `int_gpio_num`과 `poll_period_ms`가 있다. `int_gpio_num = -1`이면 주기 폴링으로 수신을 확인한다. 기본 매크로의 `poll_period_ms`는 0이라, 폴링 모드에서는 0보다 큰 값을 직접 넣어야 한다 | [R5] esp_eth_mac_spi.h, [R9] |
| ESP-IDF 6.x 이후 | SPI 이더넷 모듈 드라이버는 IDF 본체가 아니라 Component Registry 컴포넌트로 배포된다(제3자 글은 `espressif/w5500` 컴포넌트로 서술) | [R7] ESP-IDF v6.1 Ethernet 문서 |
| 판단 | **IDF 5.5.3 기준으로 지원 확인.** IDF 6.x로 올리면 의존성 선언(`idf_component.yml`)이 추가된다. S3 대상 동작은 IDF 공식 지원 대상 표로 원문 대조가 필요하다(검색으로는 S3 예제·제3자 가이드만 확인) | — |

### 2.2 소켓 수(8)와 T-05 6접속(D-029)

| 구현 방식 | 소켓 제한 | T-05(6접속, 빌드 8) | 시험 펌웨어 코드 영향 |
|---|---|---|---|
| **(가) ESP-IDF esp_eth W5500(MAC RAW) + lwIP** — 기존 코드 방식 | W5500 하드웨어 소켓은 0번 하나만 쓴다. TCP 접속 수는 lwIP 설정이 정한다. servo1axis 설정: `LWIP_MAX_SOCKETS = 10`, `LWIP_MAX_ACTIVE_TCP = 16`, `LWIP_MAX_LISTENING_TCP = 16` [E8] | 6접속 + 대기 소켓 1 = 7 ≤ 10. 8접속 빌드는 9 ≤ 10으로 여유가 1뿐이다 [산술] → DC-27로 `LWIP_MAX_SOCKETS` ≥ 16 권고 | 없음(lwIP 소켓 API 그대로) |
| (나) W5500 하드웨어 TCP(WIZnet ioLibrary, 소켓 오프로드) | 하드웨어 소켓 8개 [R8]. W5500 소켓 하나가 접속 하나를 받으므로, 502번 포트에 대기 소켓을 접속 수만큼 열어 둔다 [가정: 일반적 사용법, 데이터시트 원문 대조 필요] | 6접속에 소켓 6개 + 재대기용 1개. 8접속 빌드면 8개를 모두 써서 PC 도구·기타 서비스용 소켓이 남지 않는다 [산술] | tfw_modbus 소켓 계층을 새로 작성해야 한다. lwIP를 쓰지 않으므로 esp_netif 연동도 달라진다 |

- **권고 (가).** 소켓 8개 제한은 (가)에서는 Modbus 접속 수에 적용되지 않는다. T-05 6접속(D-029)은 lwIP 설정으로 충족한다. (나)는 CPU 부하가 낮다는 장점이 있지만, 코드 재작성 범위와 소켓 고갈 위험 때문에 채택하지 않는다.

### 2.3 SPI 클록과 처리량

| 항목 | 값 | 근거 |
|---|---|---|
| W5500 SPI 최대 | 80 MHz(제조사 표기), SPI 모드 0/3 | [R8] |
| 기존 코드 설정 | 20 MHz, SPI2_HOST, DMA 자동 | [E2] |
| S3 SPI2 IO MUX 전용 핀 | GPIO10(CS)·11(MOSI)·12(SCLK)·13(MISO) | [R10](Waveshare 튜토리얼, ESP-IDF SPI 슬레이브 문서에서 CS·SCLK 핀 일치) |
| GPIO 매트릭스 경유 시 상한 | 2차 자료는 약 40 MHz로 서술. ESP-IDF 슬레이브 문서는 80 MHz 이하에서 IO MUX와 같다고 서술. **상충, 미확인** | [R10] |
| W5500 실효 처리량(ESP32 + esp_eth) | **미확인.** 참고로 ATmega328P + W5500(SPI 약 8 MHz)에서 약 3 Mbps라는 포럼 보고가 있다(주소·제어 바이트 오버헤드). ESP32 측정값은 찾지 못했다 | [R11] |
| Modbus 부하 추정 | T-05 조건(부하 클라이언트 4개, FC03 125레지스터 응답 = MBAP 7 + PDU 252 = 259바이트, 이더넷·IP·TCP 헤더 포함 약 313바이트/프레임 [산술]). 클라이언트당 초당 100요청 [가정]이면 응답 방향 약 125 kB/s ≈ 1.0 Mbit/s, 요청·ACK 포함 양방향 합계 약 2 Mbit/s 이하 [산술] | — |
| 판단 | 20 MHz SPI(원시 20 Mbit/s)에서 오버헤드를 크게 잡아도 추정 부하의 수 배 여유가 있을 것으로 본다 [가정]. 확정은 T-16. **IO MUX 전용 핀(GPIO10~13) 사용을 권고**한다(클록 상향 여지, 매트릭스 상한 논란 회피) | — |

### 2.4 ISR·펄스 생성과의 간섭 위험

| # | 위험 | 내용 | 대책 | 확인 |
|---|---|---|---|---|
| W-1 | **GPIO ISR 서비스 공유** | 시험 펌웨어는 비상정지 입력을 `gpio_install_isr_service()` + `gpio_isr_handler_add()`로 처리하고 레벨 3(IRAM)으로 설치한다 [T7]. ESP-IDF W5500 드라이버도 INT 핀을 GPIO ISR 서비스로 받는 구조로 본다 [가정: 드라이버 원문 대조 필요]. GPIO ISR 서비스는 칩 전체에 인터럽트 하나라서, **수신 프레임마다 레벨 3 GPIO 인터럽트가 비상정지와 같은 코어에서 발생한다.** 그 코어가 실시간 코어(PCNT·GPTimer ISR, 레벨 2·1)이면 정지 ISR과 갱신 ISR을 선점한다 | (a) GPIO ISR 서비스와 W5500 INT를 통신 코어(0)에 둔다. 비상정지 2차(소프트웨어) 경로도 코어 0에서 돈다. 1차(외부 게이트)·1.5차(MCPWM 고장 입력)는 하드웨어 경로라 코어와 무관하다. (b) W5500 폴링 모드(`int_gpio_num = -1`, `poll_period_ms` 1~5 [가정])로 INT 인터럽트를 없앤다. 대신 응답 지연이 최대 폴링 주기만큼 늘어난다 | T-17에서 (a)·(b)를 비교한다(T-08 지연 분포, T-07 2차 경로 응답, NFR-010 응답 시간) |
| W-2 | SPI 전송 중 CPU 점유 | W5500 드라이버가 SPI 전송을 폴링 전송으로 하는지 인터럽트·DMA로 하는지 미확인이다. 폴링 전송이면 1,500바이트 프레임 하나에 20 MHz에서 약 0.6 ms 동안 해당 코어를 점유한다 [산술] | 드라이버 수신 태스크, SPI ISR, lwIP `tcpip` 태스크를 모두 코어 0에 고정한다(`CONFIG_LWIP_TCPIP_TASK_AFFINITY`, 드라이버 태스크 코어 설정 [가정: 설정 항목 존재 확인 필요]) | T-05(S3 빌드)에서 갱신 지연 초과 0과 `upd_max_us`로 확인 |
| W-3 | 브로드캐스트·멀티캐스트 수신 부하 | MAC RAW 모드에서는 프레임 필터링을 W5500 MAC 필터 설정에 맡긴다. 드라이버 헤더에 MAC 필터 비트가 정의되어 있으나 [R6], 활성화 여부와 브로드캐스트 처리 방식은 미확인이다. 공장망 브로드캐스트 폭주 시 SPI·lwIP 부하가 커진다. EMAC(P4)는 DMA로 받아 CPU 부하가 상대적으로 작다 | 코어 분리(W-2), 수신 큐 상한 [가정] | T-18(브로드캐스트 부하 중 펄스 오차 0, 링크 유지) |
| W-4 | 플래시 쓰기 중 SPI 이더넷 | NVS 쓰기 중 캐시가 꺼지면 IRAM이 아닌 코드가 멈춘다. 이더넷 수신이 잠시 멈출 뿐 펄스에는 영향이 없을 것으로 본다 [가정] | 운전 중 저장 보류(MOD-12, 기존 방침) | T-06(S3 빌드) |
| W-5 | SPI 버스 공유 | W5500 전용 SPI2 버스를 쓰면 플래시(SPI0/1)와 공유하지 않는다 [가정: S3 GP-SPI 구성 원문 대조] | 다른 SPI 장치를 같은 버스에 두지 않는다 | 설계 규칙 |

- 결론: 간섭은 **구조상 피할 수 있다**(코어 분리 + INT 경로 선택). 다만 W-1은 P4 기준선에는 없던 새 위험이라 실측이 필요하다(T-17).

---

## 3. 검토 3 — P4 대비 잃는 것과 얻는 것

### 3.1 잃는 것

| # | 항목 | 영향 | 회복 방법 |
|---|---|---|---|
| L-1 | **1 MHz 운전 실적(D-028)과 T-01·T-02·T-14(1축) 대체 근거** | G3 근거와 마케팅의 "검증 사실" 표기(D-031)를 S3에 쓸 수 없다. 1축 모델도 재검증 대상이 된다 | T-15(단축 주파수·계수), T-19(실모터 1 MHz) |
| L-2 | 온보드 IP101 PHY + RJ45 + 완성 보드(플래시·전원 포함) | 이더넷 회로(W5500 또는 W5500 모듈, 25 MHz 수정, 자기 결합 RJ45, 3.3 V 전류)를 캐리어에 넣어야 한다. 보드를 그대로 쓰는 F1의 "회로 위험 최소" 이점이 사라진다. 다만 F1도 캐리어 PCB는 필요했다(DC-07) | PS-30(모듈형/칩형 비교), 캐리어 설계 담당 부재 이슈(architecture 이슈 #10)가 더 급해진다 |
| L-3 | 내장 EMAC(DMA, 100 Mbit/s) | 처리량 상한이 SPI로 낮아지고 수신 처리에 CPU가 더 든다(W-2·W-3) | T-16, T-18 |
| L-4 | CPU 400 MHz RISC-V → 240 MHz Xtensa | 펄스는 하드웨어 생성이라 직접 영향은 작다. 갱신 ISR·Modbus 처리 여유가 줄고, 툴체인과 IRAM 점검 스크립트가 바뀐다 | T-05·T-09 지표 |
| L-5 | ETM(P3' 하드웨어 정지 후보) | S3 지원 여부 미확인. 기준 설계는 ETM을 전제하지 않는다 | 영향 작음 |
| L-6 | servo1axis IP101 경로 실적(D-027: HMI는 IP101로 연결) | HMI 연결 실적이 IP101 기준이다. W5500 경로는 코드가 있고 맵 문서에 EasyView 연결 대상으로 적혀 있으나 [E6], 운전 기록은 미확인이다(현행 코드는 초기화 실패를 무시 [E5]) | T-16, T-05(S3) |
| L-7 | 무선 미내장(P4) | S3는 Wi-Fi·BLE를 내장한다. 무선을 쓰지 않아도 무선 인증(KC) 대상인지는 미확인이다(R-05 재부상). 참고로 P4 모듈(F2)도 ESP32-C6를 탑재해 같은 문제가 있었다(PARTS-001) | PS-32 |

### 3.2 얻는 것

| # | 항목 | 근거·한계 |
|---|---|---|
| G-1 | 단가 절감(사용자 추정 약 1만원) | **미검증.** BOM-001의 F1 보드는 USD 12.99(수량 1 소매가)뿐이고, S3 모듈·W5500·RJ45 가격은 이 문서 범위 밖이다 → PS-30 |
| G-2 | P4 단종 통지 위험 회피 | **부분적 이점.** PCN202600801(2026-05-20)은 P4NRW32(칩 리비전 v1.3)를 단종하고 P4NRW32X(v3.x)를 권고하는 통지다(PARTS-001 P25). P4 계열 전체 단종이 아니다. 실제 위험은 "Waveshare 보드의 실장 리비전과 공급 지속이 미확인(R-20)"이다. S3로 바꾸면 이 보드 의존 위험이 없어진다. S3 자체의 장기 공급 약속은 **미확인**(PS-30) |
| G-3 | 자원 확인 수준 상승 | S3는 MCPWM 그룹당 타이머 3개, PCNT 4유닛·유닛당 2채널이 데이터시트로 확인된다. P4는 같은 항목이 미확인이었다(architecture §4.3) |
| G-4 | GPIO 여유 | 옥탈 PSRAM 미탑재 모듈 기준 약 38개 대 필요 30~32 [산술]. P4 보드 잔여 27개 [S14] 대비 R-18이 완화된다 |
| G-5 | 생태계 | ESP32 계열 MCPWM/PCNT 펄스 생성 라이브러리(FastAccelStepper)가 S3를 지원한다(competitor-analysis.md [S7]). 다만 그 문서상 상한 200 kSteps/s는 라이브러리 설계값이며 하드웨어 한계가 아니다(CON-005) |
| G-6 | 네이티브 USB(USB Serial/JTAG) | RTU 옵션 모델이나 현장 설정에서 PC 도구·펌웨어 갱신 경로 후보가 된다(§4). 핀은 GPIO19·20 [R12] |
| G-7 | 기존 W5500 코드 재사용 | servo1axis `start_w5500_interface()`가 IDF 5.5.3에서 컴파일되는 상태다 [E2][E8]. 이식 범위가 이더넷 초기화로 한정된다(§1.4) |

---

## 4. 검토 4 — 절감안 B: TCP 모델 / RTU 모델 옵션 분리

### 4.1 RTU 모델의 HMI 연결 구조

```
 [RTU 모델]  터치패널(Modbus RTU 마스터) ──RS-485(2선)── 컨트롤러(Modbus RTU 슬레이브, MOD-16)
             (선택) PC 도구 ── USB-RS485 변환기 ──┘  ※ 같은 버스에 마스터 2개 불가
```

| 항목 | 검토 |
|---|---|
| 패널 쪽 | 이지뷰(Weintek 여부 미확인)·M2I 패널이 RS-485 COM 포트에서 Modbus RTU 마스터로 동작할 수 있는지, 5/7인치 후보 기종의 RS-485 포트 유무는 **미확인**(PS-29에 추가 요청) |
| 컨트롤러 쪽 | RTU 슬레이브(MOD-16)는 이미 신규 모듈로 계획되어 있다. 레지스터 맵은 TCP와 같은 표(MOD-14 공통)를 쓰므로 화면 주소 체계는 유지된다. 단, 패널 화면 데이터의 통신 장치 설정(TCP → RTU, 국번, 통신 속도)은 SKU마다 따로 만들어야 한다 → 화면 데이터가 메이커 2종 × 통신 2종 = 4벌(D-021·D-022 범위 증가) |
| 기존 RS-485 RTU 마스터와의 충돌 | servo1axis의 RTU 마스터는 **드라이브용이 아니라 릴레이 보드 시험용**이다. 슬레이브 주소 0xFF, FC05로 릴레이 1~4 전환, FC01·FC02로 상태를 되읽는다 [E3]. architecture §3.2는 이 마스터를 제품 범위에서 이미 제외했다. 컨트롤러가 HMI의 슬레이브가 되면 같은 RS-485 버스에서 마스터를 겸할 수 없다(Modbus RTU는 단일 마스터 [가정: Modbus 직렬선 규격 원문 미대조]). **결론: 제품에서는 RTU 마스터를 넣지 않는다(기존 방침 유지).** 릴레이·I/O 확장이 필요해지면 두 번째 UART와 RS-485 포트가 필요하며 별도 결정 사항이다 |
| 통신 성능 [산술] | FC03 32레지스터 읽기 1회: 요청 8바이트 + 응답 69바이트 = 77바이트 × 10비트. 115,200 bps에서 약 6.7 ms + 프레임 간격, 9,600 bps에서 약 80 ms + 프레임 간격(3.5문자 ≈ 4 ms × 2). 2축 상태·파라미터 화면 갱신 주기가 TCP보다 길어진다. NFR-010 응답 시간 요구값이 미확인이라 합부를 판단할 수 없다 |
| 동시 접속 | RTU 버스는 마스터가 하나라서 HMI와 상위 PLC·PC 도구를 동시에 쓸 수 없다. NFR-023(동시 접속)과 T-05의 6접속 조건은 RTU 모델에 적용할 수 없다 |
| PC 도구(FR-016)·펌웨어 갱신(FR-018) | DC-09(PC 도구 = 이더넷)가 RTU 모델에서 성립하지 않는다. 대안: (1) RS-485 경유(HMI를 분리하고 접속), (2) USB 포트(S3 네이티브 USB, G-6. P4 보드는 USB-C가 있으나 [P23] 제품 노출 여부는 미정). 어느 쪽이든 MOD-21·MOD-17에 경로가 추가된다 |

### 4.2 D-017·D-019·SRS와의 관계

| 대상 | RTU 전용 SKU(이더넷 미실장) | TCP 표준 + RTU 옵션 실장 |
|---|---|---|
| D-017(HMI는 Modbus TCP), D-019(물리층 유선 이더넷 확정) | **충돌.** 새 결정으로 대체해야 한다("D-017·D-019 대체"는 사용자 권한) | 충돌 없음 |
| FR-005(TCP 필수, RTU 필수 [가정]) | 개정: 지원 계층을 SKU별로 정의 | 개정(소): RTU 필수 [가정] → 옵션 |
| FR-022(유선 이더넷 Modbus TCP 서버, 필수) | 개정: TCP SKU 한정 | 영향 없음 |
| FR-023(HMI용 데이터·명령을 Modbus TCP로 제공) | 개정: "Modbus TCP 또는 RTU" | 영향 없음 |
| FR-016(PC 도구, 유일한 설정 경로) | 개정: RTU SKU의 물리 경로 정의 | RTU 옵션 장착품도 이더넷이 있으므로 영향 없음 |
| FR-018(펌웨어 갱신), FR-024(이지뷰·M2I 기본 화면) | 경로·화면 데이터 2벌 | 영향 없음 |
| NFR-003(통신 부하 중 지터), NFR-010, NFR-023 | 시험 조건을 SKU별로 나눔 | NFR-003 시험에 RTU 부하 포함 여부만 정리 |
| CON-002(HMI는 Modbus TCP) | 개정 | 영향 없음 |
| D-015(1축/2축 2종) | SKU 4종(1·2축 × TCP·RTU) | SKU 2종 + 옵션 1개 |
| **SRS 개정 필요 여부** | **필요.** G2 기준선(v0.7, D-023) 이후의 요구 변경이므로 requirements-analyst가 SRS v0.8 개정안을 쓰고 사용자가 G2 변경을 승인해야 한다 | **필요(경미).** FR-005 RTU 항목의 필수 [가정] → 옵션. Q23(RTU 병행 여부) 답변으로 처리할 수 있다 |

### 4.3 옵션 구현 방식 비교

| 안 | 내용 | 원가 | 개발·운영 | D-017·D-019 | 판정 |
|---|---|---|---|---|---|
| B-1 | SKU별 별도 PCB | 실장 부품 최소 | PCB 2~4종 설계·검증, 재고 분산 | RTU SKU 충돌 | 비권고 |
| B-2 | 공용 PCB, 부품 미실장으로 TCP SKU / RTU SKU 구분(이더넷과 RS-485 중 하나만 실장) | TCP SKU: RS-485(ISO1500 USD 약 2.37 @100 [BOM-001 §4]), 단자, 절연 전원(미확인) 절감. RTU SKU: W5500·RJ45 절감(**A 채택 시에만**. P4 F1은 보드에 PHY·RJ45가 붙어 있어 절감 없음) | PCB 1종, 단일 펌웨어. 화면 데이터 2벌, 시험 조건 2벌 | RTU SKU 충돌 | 조건부(사용자가 RTU 전용 수요를 확인할 때만) |
| **B-3** | **공용 PCB, TCP 표준 + RS-485 옵션 실장**(기본 미실장, 주문 시 실장) | 기본품에서 RS-485 부품 절감(B-2의 TCP SKU와 같음) | PCB 1종, 단일 펌웨어(실장 검출), 화면 데이터 1벌 유지 | 충돌 없음 | **권고** |
| B-4 | 현행(둘 다 실장, SKU 분리 없음) | 절감 없음 | 가장 단순 | 충돌 없음 | 기준 비교용 |

- 펌웨어 SKU 식별: architecture §4.3 GPIO 집계에 이미 모델 식별 2비트가 있다. 2비트면 1축/2축 × RTU 실장 유무 4가지를 표현할 수 있다. 보조로 실장 자동 검출을 둔다. W5500은 SPI로 버전 레지스터를 읽어 검출할 수 있다 [가정: 레지스터 값은 데이터시트 원문 대조]. RS-485는 트랜시버 유무 검출 방법이 미확인이라 스트랩을 기준으로 한다 → DC-26.

---

## 5. 결정 후보 (사용자 확정 필요 → decisions.md)

| ID | 결정 후보 | 추천 | 근거 | 확정 시점 |
|---|---|---|---|---|
| DC-20 | 절감안 A(ESP32-S3 + W5500) 처리 | **조건부 병행 평가.** G3까지 기준선은 P4(D-024) 유지. 시험 펌웨어에 S3 포트(§1.4)를 추가하는 것을 D-025 범위로 허용하고, D-030(2축 실측 보류)을 해제해 P4와 S3에서 T-03~T-09와 T-15~T-19를 같은 절차로 실측한다. **S3 채택 조건:** (1) T-15·T-03·T-04 합격(1 MHz 단축, 2축 동시 ≥ 200 kHz, 오차 0), (2) T-05·T-17·T-18에서 펄스 오차 0, 갱신 지연 초과 0, 6접속 거부 0, (3) PS-30에서 원가 절감 확인, (4) PS-32 무선 인증 부담 수용 가능. 하나라도 불합격이면 P4 유지 | §1~§3 | 즉시(병행 평가 여부, 사용자) / G3(채택) |
| DC-21 | S3 이더넷 구현 방식 | ESP-IDF esp_eth W5500 드라이버(MAC RAW + lwIP). SPI2 IO MUX 전용 핀(GPIO10~13), 기본 20 MHz(기존 값 [E2]), T-16에서 40 MHz 이상 검토. 하드웨어 TCP 오프로드(ioLibrary)는 채택하지 않음 | §2.1~§2.3 | G3 |
| DC-22 | S3 코어·인터럽트 배치 | 코어 0: lwIP·W5500 수신 태스크·SPI ISR·GPIO ISR 서비스(비상정지 2차 경로 포함). 코어 1: GPTimer 갱신 ISR·PCNT 정지 ISR. W5500 INT 사용(a)과 폴링(b)은 T-17 결과로 선택 | §2.4 W-1·W-2 | T-17 후(G3) |
| DC-23 | S3 모듈 선정 조건 | 옥탈 PSRAM 미탑재 모듈(GPIO33~37 확보), 동작 온도 0~55 ℃ 이상(NFR-012), 노출 GPIO ≥ 32 [산술 기준]. 핀맵은 interface-spec §2를 새로 작성(P4 핀 번호 재사용 불가). MISO에 스트래핑 GPIO3 사용 금지 | §1.2 | G3(PS-30 후) |
| DC-24 | 절감안 B의 형태 | **B-3 권고: 공용 PCB, TCP 표준 + RS-485(RTU 슬레이브) 옵션 실장.** RTU 전용 SKU(B-2)는 RTU 전용 수요가 확인될 때만 검토 | §4.2·§4.3 | G3 |
| DC-25 | (RTU 전용 SKU를 택할 경우) 조건 | (1) D-017·D-019 대체 결정, (2) SRS v0.8 개정(FR-005·016·018·022·023·024, NFR-003·010·023, CON-002)과 G2 변경 승인, (3) RTU 모델 PC 도구·갱신 경로 = USB(S3) 또는 RS-485, (4) 패널 RTU 마스터 / 컨트롤러 슬레이브, 단일 마스터, RTU 마스터(릴레이 보드용)는 제품 제외, (5) 화면 데이터 4벌 | §4.1·§4.2 | B-2 선택 시(사용자 → ②) |
| DC-26 | SKU 식별 | 모델 식별 스트랩 2비트 = 축 수(1/2) × RTU 실장 유무. W5500 SPI 검출은 보조 진단으로만 사용 | §4.3 | G3 |
| DC-27 | lwIP 소켓 상한 | MCU와 무관하게 `CONFIG_LWIP_MAX_SOCKETS` ≥ 16 [가정]. 현행 10 [E8]이면 T-05 8접속 빌드에서 여유가 1뿐이다. PC 도구 등 추가 소켓을 고려한다(DC-19·D-029 연계) | §2.2 | T-05 전 |

- 신규 8건(DC-20~DC-27). 기존 DC-01(MCU), DC-03(이더넷), DC-06(RS-485), DC-07(공용화), DC-09(PC 도구), DC-13(원가), DC-14(포트)는 DC-20·DC-24 결과에 따라 갱신된다(§8).

---

## 6. 추가 실측 항목 (T-15~T-20)

시험 펌웨어 S3 빌드(DC-20) 기준. 기존 T-03~T-09는 S3 빌드로 같은 조건을 반복한다(별도 ID를 만들지 않고 보고서에 대상 칩을 적는다).

| ID | 항목 | 조건 | 합격 기준 | 펌웨어 | 관련 |
|---|---|---|---|---|---|
| T-15 | S3 단축 최고 주파수·펄스 수 정확도 (T-01·T-02의 S3 재실측) | 1축, 무부하. 100 k·200 k·500 kHz·1 MHz 주기 외부 측정. 200,000 펄스 × 3(200 kHz), 1,000,000 펄스(1 MHz) 내부 루프백 + 외부 계수 | ≥ 200 kHz(NFR-001), 오차 0(NFR-004). 1 MHz는 기록(D-028 수준 확인) | TFW-01·02·05(S3) | L-1, PCNT 입력 상한 미확인 |
| T-16 | W5500 SPI 처리량·Modbus 응답 시간 | SPI 20/40 MHz(IO MUX 핀), TCP 처리량 측정(iperf류 [가정]), FC03 125레지스터·FC16 응답 시간 분포, 6접속 | 처리량·응답 시간 기록(NFR-010 미확인). 6접속 거부 0 | TFW-03·05(S3) | §2.3, DC-21 |
| T-17 | W5500 INT / 폴링 간섭 비교 | (a) INT + GPIO ISR 서비스 코어 0, (b) 폴링 1·5 ms [가정]. T-05 부하 + 2축 운전 중 | 펄스 오차 0, 정지 ISR 최악 지연(T-08 지표)·갱신 지연 초과 0, 비상정지 2차 경로 응답(T-07 지표) 기록 | TFW-01~05(S3) | W-1, DC-22 |
| T-18 | 브로드캐스트 부하 | 2축 운전 중 브로드캐스트 프레임 1,000·10,000 프레임/s [가정] 주입 + HMI 1접속 | 펄스 오차 0, 링크·접속 유지, 응답 시간 기록 | TFW-01~05(S3), 부하 도구(호스트 추가 기능) | W-3 |
| T-19 | S3 실모터 운전 (T-14 1축 부분의 S3 재실측) | 1축 실제 드라이브·모터, 200 kHz·1 MHz | 지령 펄스 = 드라이브 수신 펄스(가능 시), 운전 기록 남김 | TFW-01~03(S3) | L-1, D-031 표기 근거 |
| T-20 | (B 채택 시) RTU 슬레이브 + HMI RTU 마스터 | 패널 RTU 마스터, 9,600·115,200 bps, 2축 운전 중 상태·파라미터 화면 상시 갱신 | 펄스 오차 0, 응답 시간·화면 갱신 주기 기록, 통신 오류 0 | ④ MOD-16(시험 펌웨어 범위 밖 — 범위 확장 시에만 G3 전 실시) | §4.1 |

- 합계 6건. T-15~T-19는 DC-20(병행 평가)이 채택될 때, T-20은 DC-24에서 RTU 옵션이나 SKU가 채택될 때만 실시한다.
- 장비 추가 [가정]: ESP32-S3 개발 보드(옥탈 PSRAM 미탑재 모듈 탑재품) 1대, W5500 모듈 1개, 브로드캐스트 발생용 PC. 나머지는 architecture §6.2와 같다.

---

## 7. parts-selector 재조사 요청

| 요청 ID | 블록 | 요구 사양(설계 측 조건) | 관련 |
|---|---|---|---|
| PS-30 | B-MCU + B-ETH(S3안) | ESP32-S3 모듈 후보(옥탈 PSRAM 미탑재, 플래시 ≥ 8 MB [가정]): 단가(수량 시나리오별), 노출 GPIO 목록, 동작 온도, 장기 공급 정책. W5500 칩 + 25 MHz 수정 + 자기 결합 RJ45 구성과 W5500 모듈(RJ45 일체형) 구성의 단가·치수·절연 내압. F1(P4 보드) 대비 차액(사용자 추정 약 1만원 검증) | DC-20, DC-23, G-1, R-06 |
| PS-31 | B-RTU 옵션 | ISO1500 + 절연 전원 + 단자 + 보호 소자의 옵션 실장 시 차액(사용자 추정 약 5천원 검증). 미실장 시 PCB 영향(단자 자리) | DC-24 |
| PS-32 | 인증 | ESP32-S3 모듈 탑재 제품의 국내 무선 인증(KC) 필요 여부와 모듈 인증 활용 가능 여부, 무선 비활성 펌웨어 시 요건. 이지뷰·M2I 후보 패널의 RS-485 포트·Modbus RTU 마스터 지원(PS-29에 추가) | L-7, R-05, §4.1 |

---

## 8. 권고가 채택될 때의 문서 개정 범위 (이번에는 수정하지 않음)

### 8.1 DC-20에서 S3 채택(G3) 시

| 문서 | 개정 범위 |
|---|---|
| architecture.md | §1 #3·#8·#9·#10(EMAC → W5500, 무선 내장), §2.1 블록도(ETH = W5500 SPI, MCU = S3), §2.2 B-MCU·B-ETH, §2.3 전면 개정(W5500 MAC RAW + lwIP, SPI2 IO MUX, INT/폴링), §2.8·§2.9 후보 부품, §3 코어 배치 문장, §3.1 1차 포트 port_esp32s3, §3.2 MOD-04 대응(W5500 경로 '제거' → '재사용'), §4.1 M2 판정(제외 → 기준선)과 P4를 대안으로, §4.3 S3 자원표로 교체(이 문서 §1.1), §4.4 조합 C8(S3 + MCPWM/PCNT + W5500) 신설, §4.5 전환 조건(P4 복귀 조건 포함), §4.6 R-01·R-05·R-18·R-20 갱신과 R-22~(W-1~W-3), §5 원가 형태, §6.1 T-01·T-02·T-14 대체 해제와 T-15~T-19 추가, §6.2 장비, §6.3 TFW 대상 칩, §7 PS-30~32, §8 DC-01·03·05·07·13·14 갱신, §9 출처 R1~R12 |
| interface-spec.md | §2 핀맵 전면 재작성(S3 핀, W5500 SPI2 핀, UART 핀, 스트래핑 회피). §5 레지스터 맵은 **변경 없음** |
| modules/ | MOD-01(sdkconfig 기준선), MOD-02(port, 예약 핀), MOD-03(핀), MOD-04(W5500 초기화, INT/폴링 설정), MOD-05(UART 핀), MOD-10(비상정지 ISR 코어·GPIO ISR 서비스 공유), MOD-15(lwIP 소켓 상한), MOD-19(모델 식별 2비트 의미), TFW-01~05(대상 칩·빌드 구성) |
| handoff.md | §2 핵심 내용(기준선), §3 결정 후보, §6 추적표의 B-ETH 표기(IP101 → W5500), §7.1 대체 항목 해제 |

### 8.2 DC-24 B-3(TCP 표준 + RTU 옵션) 채택 시

| 문서 | 개정 범위 |
|---|---|
| architecture.md | §2.2 B-RTU(옵션 실장), §2.8 모델 표에 RTU 옵션 열, §5 원가(옵션 분리), §8 DC-06·DC-07 갱신 |
| interface-spec.md | 모델 식별 레지스터 값 정의(RTU 실장 비트) |
| modules/ | MOD-05·MOD-16(옵션 실장 시에만 기동), MOD-19(실장 검출) |
| SRS(requirements-analyst, 이슈로 요청) | FR-005 RTU 필수 [가정] → 옵션(Q23 답변으로 처리) |

### 8.3 B-2(RTU 전용 SKU) 채택 시 추가

- SRS v0.8: §4.2 표 전 항목. decisions: D-017·D-019 대체, D-015 SKU 확장. architecture §1 #3, §2.1(RTU 모델 블록도), DC-09(PC 도구 경로), MOD-17·MOD-21(USB/RS-485 경로), MOD-23~25(화면 데이터 RTU판), T-20 추가.

---

## 9. 위험 (이 검토에서 새로 나온 것)

| ID(제안) | 내용 | 대응 |
|---|---|---|
| R-22 | (S3안) GPIO ISR 서비스를 W5500 INT와 비상정지가 함께 써서 실시간 코어 ISR이 선점될 수 있음(W-1) | DC-22, T-17 |
| R-23 | (S3안) MAC RAW 수신으로 인한 브로드캐스트 부하·SPI CPU 점유(W-2·W-3) | 코어 분리, T-18 |
| R-24 | (S3안) 1 MHz 실적 상실로 마케팅 성능 표기 근거 소멸(D-031 연계) | T-15, T-19 |
| R-25 | (S3안) 무선 내장으로 인증 부담 가능성 | PS-32 |
| R-26 | (B-2) RTU 전용 SKU의 G2 기준선 변경, 화면 데이터 4벌, 동시 접속 불가 | DC-24 권고(B-3) |

---

## 10. 출처 (확인일 2026-10-30, 웹 검색 결과 요약 기준)

| ID | 출처 | 확인 내용 | 비고 |
|---|---|---|---|
| R1 | Espressif ESP32-S3 Series Datasheet https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf (사본 https://www.mouser.com/datasheet/2/451/esp32_s3_datasheet_en-3361701.pdf , 구판 https://cdn-shop.adafruit.com/product-files/5477/esp32-s3_datasheet_en.pdf ) | Xtensa LX7 2코어 최대 240 MHz, 45 GPIO, MCPWM 2개(구판은 1개로 서술), MCPWM당 PWM 타이머 3·오퍼레이터 3, PCNT 4유닛·유닛당 2채널, 계수 1~65,535 | 원문 미열람. 구판·신판 MCPWM 개수 서술 차이 있음 → 신판·SoC caps 기준 2 |
| R2 | ESP32-S3 Technical Reference Manual(예비판) https://cdn-learn.adafruit.com/assets/assets/000/110/710/original/esp32-s3_technical_reference_manual_en.pdf | MCPWM·PCNT 장 | **미열람**. 주기 레지스터 폭, PCNT 입력 상한, IRAM 주소 범위 대조 필요 |
| R3 | ESP-IDF SoC Capabilities(ESP32-S3) https://docs.espressif.com/projects/esp-idf/en/release-v5.4/esp32s3/api-reference/system/soc_caps.html , https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/soc_caps.html | `SOC_MCPWM_GROUPS` = 2. `SOC_MCPWM_TIMERS_PER_GROUP`, `SOC_MCPWM_GENERATORS_PER_OPERATOR`, `SOC_PCNT_UNITS_PER_GROUP` 매크로 존재 | 나머지 값은 검색 결과에 노출되지 않음 |
| R4 | ESP-IDF Clock Tree(ESP32-S3) https://docs.espressif.com/projects/esp-idf/en/v5.0/esp32s3/api-reference/peripherals/clk_tree.html , SourceVu `soc_periph_mcpwm_timer_clk_src_t` https://sourcevu.sysprogs.com/espressif/esp-idf/symbols/soc_periph_mcpwm_timer_clk_src_t | MCPWM 기본 클록 PLL_F160M, 160 MHz 고정 | |
| R5 | ESP-IDF `esp_eth_mac_spi.h` https://github.com/espressif/esp-idf/blob/7c281c03/components/esp_eth/include/esp_eth_mac_spi.h | `eth_w5500_config_t`의 `int_gpio_num`(-1 = 폴링), `poll_period_ms` | 특정 커밋 기준, 5.5.3 원문 대조 필요 |
| R6 | ESP-IDF `esp_eth_mac_w5500.c`, `w5500.h` https://github.com/espressif/esp-idf/blob/46c00b2478e0a9ceac7f53b9d4f5c51c392dcfa9/components/esp_eth/src/esp_eth_mac_w5500.c | 소켓 0 MAC RAW 모드, 소켓 0에 TX/RX 16 KB, MAC 필터 비트 정의 | 구 버전 소스 |
| R7 | ESP-IDF v6.1 Ethernet 문서 https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_eth.html , WIZnet maker 글 https://maker.wiznet.io/Grace_Koo/projects/esp-eth-drivers/ | IDF 6.x에서 SPI 이더넷 드라이버를 Component Registry로 배포(`espressif/w5500`) | 컴포넌트 이름은 제3자 글 |
| R8 | WIZnet W5500 문서 https://docs.wiznet.io/Product/Chip/Ethernet/W5500 , https://wiznet.io/products/ethernet-chips/w5500 , 데이터시트 v1.0.5 https://www.mouser.com/datasheet/2/443/Wiznet_W5500_DS_1%200%205-1212109.pdf | 독립 하드웨어 소켓 8개, 내부 메모리 32 KB, SPI 최대 80 MHz, 모드 0/3 | 레지스터 단위(Sn_MR, VERSIONR) 원문 대조 필요 |
| R9 | ESPHome Ethernet 컴포넌트 https://esphome.io/components/ethernet/ | SPI 폴링 모드는 ESP-IDF 5.1.4 이상 | 2차 자료 |
| R10 | Waveshare ESP-IDF 튜토리얼 SPI Master https://docs.waveshare.com/ESP32-ESP-IDF-Tutorials/SPI-Master , ESP-IDF SPI Slave(ESP32-S3) https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/spi_slave.html | SPI2 IO MUX 핀 GPIO10~13, IO MUX 약 80 MHz / 매트릭스 약 40 MHz(2차 자료), 공식 문서는 80 MHz 이하에서 동일 서술 | 상충 → 미확인 |
| R11 | WIZnet 포럼 "W5500 SPI Bottleneck" https://forum.wiznet.io/t/topic/5333 | ATmega328P + W5500, SPI 약 8 MHz에서 약 3 Mbps | 참고(ESP32 아님) |
| R12 | atomic14 ESP32-S3-WROOM-1 https://esp32.atomic14.com/modules/esp32-s3-wroom-1/ | 스트래핑 GPIO0·3·45·46, 옥탈 PSRAM GPIO35~37 | 제3자. 데이터시트 원문 대조 필요 |
| E2·E3·E5·E6·E7·E8 | servo1axis(커밋 2135568) movion_modbus_tcp.c, RS485_MODBUS_RTU_TEST.md·movion_modbus_rtu, CURRENT_STATE.md, MODBUS_TCP_REGISTER_MAP.md, hardware.json, sdkconfig | W5500 SPI2 20 MHz·`esp_eth_mac_new_w5500`·INT GPIO6, RTU 마스터 = 릴레이 보드(0xFF, FC05/01/02), W5500 초기화 실패 무시, 맵 문서상 W5500 = EasyView 대상, hardware.json W5500·ISO1500 노드, `CONFIG_ETH_SPI_ETHERNET_W5500=y`, `LWIP_MAX_SOCKETS=10` | 읽기 전용 |
| T1~T7 | project/04_code/test-fw: T1 tfw_logic.h(`TFW_PCNT_HIGH_LIMIT` 30000, `TFW_MAX_PERIOD_TICKS` 65535 [assumed]), T2 tfw_pulse2.c, T3 main/Kconfig.projbuild·tfw_main.c, T4 sdkconfig.tfw_iram_on, T5 sdkconfig.defaults, T6 tfw_modbus.c, T7 tfw_estop_io.c(GPIO ISR 서비스 레벨 3) | 이식 범위 판단 | |
| 내부 | bom-draft.md BOM-001 v0.2(F1 USD 12.99 @1, ISO1500 USD 약 2.37 @100), parts-comparison.md PARTS-001 v0.2 P25(PCN202600801) | 가격·단종 통지 | parts-selector 산출물 |

---

## 11. 이슈 제기

| # | 대상 | 내용 |
|---|---|---|
| 1 | 사용자 | DC-20 병행 평가를 하려면 D-030(2축 실측 보류) 해제와 D-025 시험 범위 확장(S3 포트)이 필요하다 |
| 2 | 사용자 / 02_requirements | RTU 전용 SKU(B-2)를 원하면 D-017·D-019 대체 결정과 SRS v0.8(§4.2 목록), G2 변경 승인이 필요하다. 권고안 B-3이면 FR-005 RTU '필수 [가정] → 옵션' 정리만 필요하다(Q23). SRS는 수정하지 않았다 |
| 3 | 사용자 | 기존 RS-485 RTU 마스터는 드라이브용이 아니라 릴레이 보드 시험용이다(RS485_MODBUS_RTU_TEST.md [E3]). 드라이브 쪽 RTU 통신 요구가 따로 있으면 알려 주기 바란다(현 SRS에는 없음) |
| 4 | parts-selector | PS-30~32(§7). 사용자 추정 절감액(A 약 1만원, B 약 5천원)의 검증을 포함한다 |
| 5 | pm-orchestrator | status.md I-043·I-044에 이 문서 경로와 판단을 연결해 달라. I-043의 "P4 칩 단종 통지 위험"은 P4NRW32(v1.3) 한정 통지라는 점(§3.2 G-2)을 함께 적어 달라 |
| 6 | 사용자 / ⑦ | S3를 채택하면 D-028(1 MHz 실적)을 제품 성능 표기 근거로 쓸 수 없다. T-15·T-19 전까지 07_marketing 초안의 1 MHz 표기는 '설계 목표(미검증)'로 바꿔야 한다(D-031) |
| 7 | pm-orchestrator | 캐리어 PCB·회로 담당 단계 부재(architecture 이슈 #10). S3안에서는 이더넷 회로까지 캐리어로 들어오므로 더 시급하다 |
