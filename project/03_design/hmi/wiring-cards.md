# 드라이브별 배선 카드 (FR-037, 초안)

컨트롤러 쪽 단자는 회로도 r2 기준으로 확정. **드라이브 쪽 단자 번호는 각 제조사 매뉴얼 확인 전이라 비워 둔다(미확인).** 사용자 보유 드라이브의 매뉴얼 또는 실물 결선 사진을 받으면 채운다.

## 컨트롤러 쪽 (공통)
| 신호 | 컨트롤러 단자 | 형식 |
|---|---|---|
| 펄스 +/− (X) | J7 PULS(X) A쌍 | 차동(AM26C31) |
| 방향 +/− (X) | J7 PULS(X) B쌍 | 차동 |
| 펄스·방향 (Y) | J13 PULS(Y) | 차동 |
| 서보 ON (X/Y) | J6 DO1 / DO2 | NPN 싱크, 24 V |
| START·STOP (X) | J15 D/I(X) DI1·DI2 | 24 V 입력, STOP은 NC |
| START·STOP (Y) | J16 D/I(Y) DI5·DI6 | 〃 |

## 드라이브별
| 드라이브 | 모델 | 펄스+ | 펄스− | 방향+ | 방향− | 서보 ON | 차동 입력 허용 주파수 | 확인 출처 |
|---|---|---|---|---|---|---|---|---|
| 대우 | C-시리즈 | CN1 13 PPA+ (펄스, D-079·D-080) | CN1 14 PPA− (D-080) | CN1 11 PPB+ (부호, D-079·D-080) | CN1 12 PPB− (D-080) | CN1 21 SVON(공통 7 COM1 [추정]) | **400 kPPS**(라인드라이버, 26LS31 호환) | 대우 서보 C-시리즈 취급 설명서 §3 사양·§4.1.1 CN1 단자배열(사용자 제공, 텍스트 판독) |
| LS | L7S | CN1 9 PF+ (D-082) | CN1 10 PF− | CN1 11 PR+ (부호 [추정]) | CN1 12 PR− | CN1 47 SVON(입력 공통 50 +24VIN) | 1 Mpps(라인드라이버), 200 kpps(오픈컬렉터) [웹 발췌, D-083] | 사용자 제공 CN1 결선도(2026-11-01) |
| 미쓰비시 | MR-J3-□A | CN1 10 PP (D-081) | CN1 11 PG | CN1 35 NP (부호) | CN1 36 NG | CN1 15 SON(DICOM 20 = +24 V, DOCOM 47 = 0 V) | 1 Mpps(차동), 200 kpps(오픈컬렉터) [웹 발췌, D-083] | 사용자 제공 CN1 결선도(QD75D 연결 예, 2026-11-01) |

필요한 자료: 사용 중인 드라이브의 모델명과 매뉴얼(또는 CN1 커넥터 핀 배치 페이지).

## 대우 C-시리즈 판독 메모 (2026-11-01)
- CN1: 3M 10236-52A2(드라이버 측), 사용자 측 10136-3000VE, 36핀.
- 판독한 단자배열(홀수/짝수 줄): 1 24GND, 3 VS1(CLR), 5 STOP, 7 COM1, 9/10 PLA, 11/12 PPB, 13/14 PPA, 21 SVON, 22 ALMRST, 23 N-OT, 24 P-OT, 25 VPF, 27 SVRDY. PPA = 펄스, PPB = 부호(사용자 확인, D-079). PPA는 13번 +, 14번 −(사용자 확인, D-080). PPB는 11번 +, 12번 −(사용자 확인, D-080). 회전 방향을 바꾸려면 드라이브 회전 방향 파라미터를 쓴다.
- 위치 지령 입력: 펄스/부호, CCW/CW, A/B 2상 지원. 최대 **400 kPPS**. 전자기어 1~9999 / 1~9999. 엔코더 2000 PPR(체배 시 8000/회전).
- **주의:** 이 드라이브는 지령 펄스 400 kPPS가 상한이다. 컨트롤러 1 MHz 출력은 이 드라이브에서는 쓸 수 없다(D-077). 실모터 운전 실적: 이 드라이브로 400 kHz까지(D-078).

## 미쓰비시 MR-J3-□A 판독 메모 (2026-11-01, D-081)
- 결선도의 QD75D 차동 연결 예: PULSE F+ → PP 10, PULSE F− → PG 11, PULSE R+ → NP 35, PULSE R− → NG 36. 우리 컨트롤러는 A채널(펄스) → PP/PG, B채널(방향) → NP/NG.
- 드라이브의 지령 펄스 입력 형태를 '펄스열 + 부호'로 설정해야 한다. 파라미터 번호는 미확인(매뉴얼 확인 필요).
- 입력 전원: DICOM 20 = 인터페이스 +24 V, DOCOM 46/47 = 0 V. 입력은 DOCOM으로 떨어뜨리는 싱크형. 컨트롤러 DO1(NPN 싱크) → SON 15, 컨트롤러 0 V와 DOCOM 공통.
- **운전에 필요한 입력:** EMG 42(비상정지, 닫혀 있어야 운전), LSP 43·LSN 44(정전·역전 스트로크 엔드, 켜져 있어야 해당 방향 운전). 배선하지 않으면 모터가 돌지 않는다. DOCOM에 단락하거나 드라이브의 입력 자동 ON 파라미터로 처리한다(파라미터 번호 미확인).
- 리셋 RES 19, 서보 준비 RD 49(출력).
- 차동 입력 허용 주파수: 결선도에 없음. 미확인.

## LS L7S 판독 메모 (2026-11-01, D-082)
- 명령 펄스 입력: PF+ 9 / PF− 10, PR+ 11 / PR− 12(라인드라이브). 오픈컬렉터일 때 PULCOM 49 사용(우리 제품은 차동만, D-046이므로 미사용).
- 우리 컨트롤러는 A채널(펄스) → PF+/PF−, B채널(방향) → PR+/PR−. 펄스+부호 모드에서 PR이 부호 입력이라는 것은 [추정]. 지령 펄스 형태 파라미터 번호와 함께 매뉴얼로 확인한다.
- 입력 공통: +24VIN 50에 +24 V, 입력은 0 V 쪽으로 닫는 방식. 컨트롤러 DO1(NPN 싱크) → SVON 47, 0 V 공통.
- **운전에 필요한 입력:** EMG 18(비상정지, NC), STOP 48, CWLIM 19·CCWLIM 20(리밋, NC). 배선하지 않으면 운전되지 않을 수 있다. 입력 기능 할당 파라미터로 끄거나 단락한다(파라미터 번호 미확인).
- 알람 리셋 ALMRST 17, 브레이크 출력 BRAKE 44, 알람 출력 ALARM+ 38 / ALARM− 39.
- 차동 입력 허용 주파수: 결선도에 없음. 미확인.

## 웹 조사 보강 (2026-11-01, D-083)
공식 매뉴얼 PDF(ls-electric.com, manualslib.com, smcetech.com, mitsubishielectric.com)는 네트워크 차단으로 열람 실패. 아래는 **검색 결과 발췌문** 기준이며 원문 대조 전이다.

| 항목 | 미쓰비시 MR-J3-□A | LS L7S |
|---|---|---|
| 지령 펄스 최대 주파수 | 차동 1 Mpps, 오픈컬렉터 200 kpps | 라인드라이버 1 Mpps, 오픈컬렉터 200 kpps |
| 지원 펄스 형태 | 펄스열+부호, 정·역전 펄스열, A/B상 | 부호+펄스, CW+CCW, A/B상 |
| 펄스+부호 설정 | PA13 = 0001h(정논리), 0011h(부논리). SMC LECSA(MR-J3-A 기반) 매뉴얼 발췌. 미쓰비시 원문 표 확인 필요 | 파라미터 번호 미확인. L7P·L7C는 0x3003(펄스 입력 논리)이나 L7S(P0~P4 그룹)와 번호 체계가 다름 |
| 입력 자동 ON | PD01로 추정(구형 J2S의 *DIA에 해당). 자리값 미확인 | 미확인 |
| 펄스 핀 | PP·PG / NP·NG (사용자 결선도) | PF+·PF− / PR+·PR− (L7S 매뉴얼 발췌에 핀 이름 일치) |

**판단:** 두 드라이브 모두 차동 1 Mpps 입력이므로 우리 컨트롤러 출력(1 MHz 실측) 범위를 그대로 쓸 수 있다. 대우 C-시리즈(400 kPPS)만 상한이 낮다.

출처(확인일 2026-11-01, 모두 검색 발췌):
- 미쓰비시 MR-J3-A 카탈로그(배포처): https://baoanjsc.com.vn/TaiLieu/mr-j3-a-type-servo-amplifier-mitsubishi-MELSERVO-J3-series-CATALOG-8263.pdf
- SMC LEC-OM05805(MR-J3-A 기반 드라이버, PA13 값): https://content2.smcetech.com/pdf/manuals/LEC-OM05805.pdf
- MR-J3-A 매뉴얼 PA13 절: https://www.manualslib.com/manual/769845/Mitsubishi-Electric-Melservo-Mr-J3-A.html?page=156
- MR-J2S 파라미터표(*DIA): https://forum.linuxcnc.org/media/kunena/attachments/22081/2016-05-29MR-J2SParameterTable.pdf
- LS L7S 사양서: https://www.ip-systemes.com/media/3416/3225-fiche-technique-l7s.pdf
- LS L7S 매뉴얼 V2.4(공식, 열람 실패): https://www.ls-electric.com/upload/customer/download/d462ae5d-d496-439e-b71e-ad374d8a843c/Manual_XDL-L7S_200V_V2.4.pdf
- LS L7P 퀵스타트(0x3003): https://cdn.automationdirect.com/static/manuals/lselectric/l7pquickstart.pdf
