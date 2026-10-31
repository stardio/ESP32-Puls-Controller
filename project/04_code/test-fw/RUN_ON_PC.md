# PC 실측 가이드 (1축 캐리어 + 축2 헤더 측정)

상세 절차와 근거는 `../notes/test-fw.md` §4에 있다. 이 문서는 실행 순서만 요약한다.

## 준비물
- ESP-IDF 5.5.3, Waveshare ESP32-P4-ETH + 1축 캐리어, 로직 분석기(4ch 이상, 10 MS/s 이상), 스위칭 허브, 24 V 전원, 기존 HMI
- STOP용 24 V 스위치(NC 접점) 1개, START용 접점 1개

## 로직 분석기 연결
| 채널 | 연결 위치 | 신호 |
|---|---|---|
| CH0 | 캐리어 J4 (축1 차동 출력, 수신 측) 또는 P4 헤더 GPIO25 | 축1 펄스 |
| CH1 | P4 헤더 GPIO32 | 축1 방향 |
| CH2 | **P4 헤더 GPIO20** | 축2 펄스 (캐리어 축2 부품 불필요) |
| CH3 | **P4 헤더 GPIO21** | 축2 방향 |
| CH4 | J15 DI2 쪽 GPIO19 | STOP 트리거 (T-07, 상승 엣지) |

## 실행 순서
1. `cd project/04_code/test-fw && idf.py set-target esp32p4`
2. 빌드 2벌(IRAM on/off) — 명령은 notes §4의 2번. **컴파일 오류가 나면 오류 메시지 전체를 그대로 알려 주세요.**
3. `idf.py -B build_on -p <PORT> flash monitor` → 부팅 로그에서 아래 4줄을 확인·기록
   - `CAPS ...`(축2 자원), `bind_ok`, `GPIO19 = DI2(J15) ... inactive`, `pulse output enable GPIO33 = 1`
   - STOP이 `ACTIVE`로 나오면 menuconfig에서 STOP 극성을 반대로 바꾼다.
4. `cd host && python3 -m tfw_host.cli run scenario_example.yaml --target 192.168.0.220:502 --results results --wait-capture`
5. `python3 -m tfw_host.cli report results reports/report_G3_pretest.md`
6. T-05만 `TFW_MAX_CLIENTS=8`로 따로 빌드해 실행한다(D-029).
7. 결과(`host/results/`, `host/reports/`)와 부팅 로그를 저장소에 push한다.

## 지켜야 할 것 (거짓 합격 방지, 리뷰 3차 §3-2)
- 모든 실측을 **결과 폴더 하나**에 쌓고 지우거나 옮기지 않는다.
- 로직 분석기 캡처는 단계마다 새로 시작한다.
- 불합격(FAIL) 기록을 다른 캡처로 다시 판정하지 않는다.

## 이번에 못 하는 것 (⑤단계로)
- 축2 출력단(U16·AM26C31 3/4채널·J14), 축2 STOP/START 입력, 2축 실모터 운전(T-14)
