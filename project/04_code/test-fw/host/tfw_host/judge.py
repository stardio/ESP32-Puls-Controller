"""Pass/fail rules, coded from architecture sec.6.1 (T-ID table). Items whose criterion is 'record' return RECORD
without a verdict. Status values: PASS, FAIL, RECORD, SUBSTITUTED (D-028), NOT_RUN."""

PASS, FAIL, RECORD, SUBSTITUTED, NOT_RUN = "PASS", "FAIL", "RECORD", "SUBSTITUTED", "NOT_RUN"

T_TITLES = {
    "T-01": "단축 최고 주파수", "T-02": "단축 펄스 수 정확도", "T-03": "2축 동시 최고 주파수",
    "T-04": "2축 동시 펄스 수 정확도(외부 계수)", "T-05": "Modbus TCP 부하 중 누락 0", "T-06": "플래시(NVS) 쓰기 간섭",
    "T-07": "비상정지 응답", "T-08": "목표 정지 ISR 지연", "T-09": "속도 갱신 경계·양자화",
    "T-10": "24 V OC 출력", "T-11": "차동 출력 5 m", "T-12": "절연 입력 응답", "T-13": "기구 수납",
    "T-14": "실제 드라이브·모터 운전", "T-15": "CPU 정지 시 출력 지속(워치독)",
}
ALL_IDS = list(T_TITLES)
SUBSTITUTED_IDS = {
    "T-01": "기존 실적으로 대체(D-028): 100/200/500 kHz·1 MHz 외부 측정, 1 MHz 운전. 근거: servo1axis CURRENT_STATE.md",
    "T-02": "기존 실적으로 대체(D-028): 200,000 펄스 x3(200 kHz), 1,000,000 펄스(1 MHz) 루프백 일치. 근거: servo1axis CURRENT_STATE.md",
}
HARDWARE_ONLY = {"T-10", "T-11", "T-12", "T-13"}


# Status values read from tst_status (0x7038)
ST_IDLE, ST_RUNNING, ST_DONE, ST_BUSY, ST_BAD, ST_NORES = 0, 1, 2, 0x10, 0x11, 0x12
ST_NAME = {ST_IDLE: "IDLE(중단)", ST_RUNNING: "RUNNING", ST_DONE: "DONE", ST_BUSY: "0x10 거부(래치/실행 중)",
           ST_BAD: "0x11 값 범위 밖", ST_NORES: "0x12 자원 없음(축2 등)"}
RUN_KEYS = ("wait_status", "timed_out", "seq_done", "err_ax1", "err_ax2", "overshoot_ax1", "overshoot_ax2",
            "cmd", "repeat", "axis_mask", "param_status", "param_mismatch", "clear_ok")
MIN_ITER_T04 = 1000          # architecture 6.1 T-04 "축당 1,000회" [가정]
NVS_MIN_RATIO = 0.5          # [가정] share of the nominal NVS writes (period + commit time) that must be seen
T05_MIN_CLIENTS = 4          # architecture 6.1 T-05 "TCP 클라이언트 4개" [가정]
DEFAULT_AVG_TOL_PCT = 0.01   # [가정] T-03 tolerance on the 10 s average frequency


def _v(status, *reasons):
    return {"status": status, "reasons": list(reasons)}


LA_KEYS = ("la_edges", "la_f_avg_hz", "la_f_max_hz", "la_latency_us_max", "la_first_periods_us")


def _la_gate(d: dict):
    """N-04: logic-analyser data count only if the CSV is proven to be the capture of THIS run (attached after the run:
    modified after the run ended, long enough, hash not used by another run). Else the data is stale -> FAIL."""
    if not any(k in d for k in LA_KEYS):
        return None
    if d.get("la_fresh") is not True:
        why = d.get("la_fresh_reasons") or ["CSV가 이 실행의 캡처라는 확인(수정 시각·길이·해시)이 없음"]
        return _v(FAIL, "로직 분석기 CSV가 이번 실행의 캡처가 아님(묵은 캡처로 합격할 수 있음): " + "; ".join(why))
    return None


def _precheck(d: dict, *, need_axes=None, min_iter=None):
    """Conditions without which a counter value of 0 means nothing (C-01). Returns a verdict dict if the run cannot
    count as a valid, completed run, else None. Order: missing data -> NOT_RUN, a run that did not complete -> FAIL."""
    # wait_status None is a valid value (timeout); every other key must be present and not None
    missing = [k for k in RUN_KEYS if (k not in d if k == "wait_status" else d.get(k) is None)]
    if missing:
        return _v(NOT_RUN, f"판정 불가: 실행 결과 항목 누락 {missing}")
    if d["param_status"] == ST_BAD:
        return _v(FAIL, "파라미터 쓰기가 0x11(범위 밖)로 거부됨: 이전 값으로 실행됐을 수 있음")
    if d["param_mismatch"]:
        return _v(FAIL, f"쓴 값과 읽은 값이 다름: {d['param_mismatch']}")
    if d["clear_ok"] is not True:
        return _v(FAIL, "CLEAR_STATS가 거부되었거나 카운터가 0으로 지워지지 않음")
    if d.get("seed_changed"):
        return _v(FAIL, "실행 중 SEED 레지스터가 바뀜: 시퀀스를 재현할 수 없음")
    ws = d["wait_status"]
    if d["timed_out"] or ws is None:
        return _v(FAIL, "타임아웃: 종료 상태를 받지 못함")
    if ws != ST_DONE:
        return _v(FAIL, f"종료 상태 {ST_NAME.get(ws, hex(ws))}: 정상 완료(DONE)가 아님")
    iters = d["repeat"] if d["cmd"] == "RUN_SEQ" else 1
    if d["seq_done"] != iters:
        return _v(FAIL, f"seq_done {d['seq_done']} != 기대 반복 {iters}")
    la = _la_gate(d)
    if la:
        return la
    if need_axes is not None and d["axis_mask"] != need_axes:
        return _v(NOT_RUN, f"기준 조건 미충족: axis_mask {d['axis_mask']} (기준 {need_axes})")
    if min_iter is not None and iters < min_iter:
        return _v(NOT_RUN, f"기준 조건 미충족: 반복 {iters} < {min_iter}")
    return None


def _counters(d):
    bad = d["err_ax1"] + d["err_ax2"]
    ovs = d["overshoot_ax1"] + d["overshoot_ax2"]
    return bad, ovs


def _external(d):
    """True / False / None (None = no external count). Needs per-axis measured AND expected edges."""
    got, exp = d.get("la_edges"), d.get("la_expected_edges")
    if not got or not exp or len(got) != len(exp) or any(e is None for e in exp):
        return None
    return all(g == e for g, e in zip(got, exp))


def _pulse_verdict(d, why, label):
    """Common ending of T-04/05/06: counters, then external count (PCNT is the output pin itself: it cannot see a lost pulse)."""
    bad, ovs = _counters(d)
    ext = _external(d)
    if bad:
        why.append(f"PCNT 불일치 {bad}")
    if ovs:
        why.append(f"목표 초과 {ovs}")
    if ext is False:
        why.append(f"외부 계수 != 기대 목표 합 (측정 {d.get('la_edges')}, 기대 {d.get('la_expected_edges')})")
    if bad or ovs or ext is False:
        return _v(FAIL, *why)
    if ext is None:
        return _v(NOT_RUN, *why, "판정 불가: 외부 계수(로직 분석기) 없음. PCNT=목표는 출력 핀 자체를 센 값이라 누락을 검출하지 못함")
    return None


def judge(tid: str, d: dict) -> dict:
    """d = measured data of one run. Returns {status, reasons[]}. Missing data never yields PASS."""
    why = []
    if tid == "T-03":
        pre = _precheck(d, need_axes=None)
        if pre:
            return pre
        step = d.get("freq_hz", 0)
        favg = d.get("la_f_avg_hz")
        if not favg or len(favg) < 2 or d.get("la_edges") is None or any(e <= 0 for e in d["la_edges"]):
            return _v(NOT_RUN, "판정 불가: 로직 분석기 측정 없음 또는 한 축 펄스 0")
        fmin = min(favg)
        if step == 200_000:
            if d["axis_mask"] != 3:
                return _v(NOT_RUN, "기준 조건 미충족: 2축 동시가 아님")
            tol = d.get("tol_pct", DEFAULT_AVG_TOL_PCT) / 100.0
            ok = fmin >= 200_000 * (1 - tol)
            return _v(PASS if ok else FAIL,
                      f"두 축 평균 주파수 최저 {fmin:.0f} Hz (기준 >= 200 kHz, 평균 측정 허용오차 {tol*100:.3f}% [가정]); "
                      f"최저 순간 주파수 {d.get('la_f_min_hz', float('nan')):.0f} Hz는 기록")
        return _v(RECORD, f"{step} Hz 단계는 기록 항목, 두 축 평균 최저 {fmin:.0f} Hz")
    if tid == "T-04":
        pre = _precheck(d, need_axes=3, min_iter=MIN_ITER_T04)
        if pre:
            return pre
        r = _pulse_verdict(d, why, "T-04")
        return r or _v(PASS, f"반복 {d['seq_done']}회 완료, 오차 0, 초과 0, 외부 계수 = 기대 목표 합")
    if tid == "T-05":
        pre = _precheck(d, need_axes=3, min_iter=MIN_ITER_T04)
        if pre:
            return pre
        bad, ovs = _counters(d)
        late = d.get("upd_late")
        if late is None:
            return _v(NOT_RUN, "판정 불가: upd_late 없음")
        if late:
            why.append(f"갱신 지연 {late}건")
        ld = d.get("tcp_load")
        clients = d.get("tcp_clients", 0)
        if not ld:
            return _v(FAIL if (bad or ovs or late) else NOT_RUN, *why, "TCP 부하 결과 없음: 부하가 걸리지 않음")
        if ld.get("refused", 0):
            why.append(f"부하 접속 거부 {ld['refused']}건: 부하가 요구한 수만큼 걸리지 않음 (max_clients {d.get('mb_max_clients')})")
        if ld.get("requests", 0) <= 0:
            why.append("부하 요청 0건")
        hmi = 1 if d.get("hmi") else 0
        want = 1 + clients + hmi
        seen = d.get("mb_clients_max")
        if seen is None:
            return _v(FAIL if (bad or ovs or late) else NOT_RUN, *why, "판정 불가: 실행 중 접속 수(mb_clients) 미측정")
        if seen < want:
            why.append(f"실행 중 최대 접속 {seen} < 기대 {want} (제어 1 + 부하 {clients} + HMI {hmi})")
        mx = d.get("mb_max_clients")
        if mx is not None and mx < want:
            why.append(f"빌드 max_clients {mx} < 필요 접속 {want}: T-05 조건을 만들 수 없음(설계 모순 T-05 6개 대 4개)")
        smin = d.get("mb_clients_min")
        if smin is None:
            return _v(FAIL if (bad or ovs or late) else NOT_RUN, *why, "판정 불가: 실행 중 최소 접속 수(mb_clients_min) 미측정")
        if smin < want:
            why.append(f"실행 중 최소 접속 {smin} < 기대 {want}: 부하가 운전 내내 유지되지 않음")
        if ld.get("dropped", 0):
            why.append(f"부하 클라이언트 {ld['dropped']}개가 운전 도중 끊김")
        if ld.get("idle_clients", 0):
            why.append(f"요청을 한 건도 처리하지 못한 부하 클라이언트 {ld['idle_clients']}개")
        if ld.get("errors", 0):
            why.append(f"부하 오류 {ld['errors']}건(타임아웃 {ld.get('timeouts', 0)}, 예외 {ld.get('exceptions', 0)}): 기록")
        if (ld.get("refused", 0) or ld.get("requests", 0) <= 0 or seen < want or (mx is not None and mx < want) or
                smin < want or ld.get("dropped", 0) or ld.get("idle_clients", 0)):
            return _v(FAIL, *why)
        if bad or ovs or late:
            return _v(FAIL, *([f"펄스 오차 {bad}, 초과 {ovs}"] if (bad or ovs) else []), *why)
        if clients < T05_MIN_CLIENTS:
            return _v(NOT_RUN, *why, f"기준 조건 미충족: 부하 클라이언트 {clients} < {T05_MIN_CLIENTS}")
        if not hmi:
            return _v(NOT_RUN, *why, "기준 조건 미충족: 실제 HMI 1대 접속이 확인되지 않음(step.hmi 미지정)")
        r = _pulse_verdict(d, why, "T-05")
        why.append("응답 시간 분포는 기록 항목")
        return r or _v(PASS, *why, f"접속 최소 {smin}·최대 {seen}, 부하 요청 {ld['requests']}건")
    if tid == "T-06":
        pre = _precheck(d, need_axes=3, min_iter=MIN_ITER_T04)
        if pre:
            return pre
        per = d.get("nvs_period_ms")
        if not per:
            return _v(NOT_RUN, "기준 조건 미충족: NVS 쓰기 주기가 설정되지 않음(부하 없음)")
        nvs = d.get("nvs_writes")
        run_s = d.get("run_s")
        cfg = d.get("cfg_flags")
        if nvs is None or run_s is None or cfg is None:
            return _v(NOT_RUN, "판정 불가: nvs_writes/실행 시간/cfg_flags 없음")
        need = max(1, int(run_s * 1000.0 / per * NVS_MIN_RATIO))
        if nvs < need:
            return _v(FAIL, f"NVS 쓰기 {nvs}회 < 기대 최소 {need}회(주기 {per} ms, {run_s:.1f} s, 비율 {NVS_MIN_RATIO} [가정]): 부하가 걸리지 않음")
        dev_iram = bool(cfg & 1)
        sc_iram = d.get("iram_safe")
        if sc_iram is not None and bool(sc_iram) != dev_iram:
            return _v(FAIL, f"시나리오 iram_safe={bool(sc_iram)} 이나 장치 cfg_flags b0={dev_iram}: 설정이 다른 빌드의 결과")
        bad, ovs = _counters(d)
        if dev_iram:
            r = _pulse_verdict(d, why, "T-06")
            return r or _v(PASS, f"IRAM 안전 on(장치 확인): 오차 0, 초과 0, 외부 계수 일치, NVS 쓰기 {nvs}회")
        return _v(RECORD, f"IRAM 안전 off(비교용, 장치 확인): 오차 {bad}, 초과 {ovs}, NVS 쓰기 {nvs}회")
    if tid == "T-07":
        why.append("정지 지연·경로 간 계수 차이는 기록 항목(NFR-011 미확인)")
        la = _la_gate(d)
        if la:
            return la
        if d.get("es_last_err") is None or d.get("es_trip_count") is None or d.get("es_trip_count_before") is None:
            return _v(NOT_RUN, *why, "판정 불가: es_last_err/es_trip_count(실행 전후) 없음")
        if d["es_last_err"] != 0:
            return _v(NOT_RUN, *why, f"전제 불충족: es_last_err=0x{d['es_last_err']:x}(초기화 실패 또는 ISR 서비스 기설치로 레벨·IRAM 설정 미적용): 경로 A 결과로 기록할 수 없음")
        if d["es_trip_count"] <= d["es_trip_count_before"]:
            return _v(NOT_RUN, *why, f"전제 불충족: 비상정지 트립이 기록되지 않음(es_trip_count {d['es_trip_count_before']} -> {d['es_trip_count']})")
        if d.get("la_trigger_edge_mismatch"):
            return _v(NOT_RUN, *why, "판정 불가: 로직 분석기 트리거 엣지가 장치 극성과 맞지 않음")
        relaunch = d.get("relaunch_after_latch")
        if relaunch is None:
            return _v(RECORD, *why, "래치 중 재기동: 미측정(합격 근거 아님)")
        if relaunch:
            return _v(FAIL, *why, f"래치 중 재기동 {relaunch}")
        return _v(RECORD, *why, "래치 중 RUN 시도 거부(0x10) 확인, 상태·seq_done 불변. 핀 펄스 0은 로직 분석기로 별도 확인")
    if tid == "T-08":
        pre = _precheck(d)
        if pre:
            return pre
        ovs = d["overshoot_ax1"] + d["overshoot_ax2"]
        return _v(FAIL if ovs else RECORD, f"최악 지연 기록; 설정 start_speed에서 초과 펄스 {ovs}")
    if tid == "T-09":
        pre = _precheck(d)
        if pre:
            return pre
        over = d.get("requests_exceeded")
        fp = d.get("first_periods_check")
        if fp is not None:
            if fp.get("bad"):
                return _v(FAIL, f"정지 후 첫 주기가 기대({fp['expected_us']:.3f} us ± {fp['tol_us']:.3f}) 밖: {[round(x, 3) for x in fp['bad']]} us "
                                f"(기대보다 {'짧음=요청 초과' if any(x < fp['expected_us'] for x in fp['bad']) else '길음=이전 주기 잔류'})")
            if fp.get("n_found", 0) < fp.get("n", 3):
                return _v(NOT_RUN, f"판정 불가: 캡처에 첫 {fp.get('n', 3)}주기가 없음({fp.get('n_found', 0)}개)")
        if over:
            return _v(FAIL, f"요청 초과 출력: {over}")
        if not d.get("freq_checked") or over is None:
            return _v(NOT_RUN, "측정 없음")
        if not d.get("la_f_max_hz"):
            return _v(NOT_RUN, "판정 불가: 로직 분석기에 펄스가 없음")
        extra = f"; 첫 {fp['n']}주기가 기대 {fp['expected_us']:.3f} us ± {fp['tol_us']:.3f} 안" if fp is not None else ""
        return _v(PASS, "요청 초과 출력 0" + extra)
    if tid == "T-14":
        return _v(RECORD, "1축은 기존 실적으로 대체(D-028); 2축 드라이브 수신 펄스 대조는 사용자 입력")
    if tid in HARDWARE_ONLY:
        return _v(RECORD if d else NOT_RUN, "하드웨어 시제 필요 항목")
    return _v(RECORD)
