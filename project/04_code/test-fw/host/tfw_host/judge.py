"""Pass/fail rules, coded from architecture sec.6.1 (T-ID table). Items whose criterion is 'record' return RECORD
without a verdict. Status values: PASS, FAIL, RECORD, SUBSTITUTED (D-028), NOT_RUN."""

PASS, FAIL, RECORD, SUBSTITUTED, NOT_RUN = "PASS", "FAIL", "RECORD", "SUBSTITUTED", "NOT_RUN"

T_TITLES = {
    "T-01": "단축 최고 주파수", "T-02": "단축 펄스 수 정확도", "T-03": "2축 동시 최고 주파수",
    "T-04": "2축 동시 펄스 수 정확도(외부 계수)", "T-05": "Modbus TCP 부하 중 누락 0", "T-06": "플래시(NVS) 쓰기 간섭",
    "T-07": "비상정지 응답", "T-08": "목표 정지 ISR 지연", "T-09": "속도 갱신 경계·양자화",
    "T-10": "24 V OC 출력", "T-11": "차동 출력 5 m", "T-12": "절연 입력 응답", "T-13": "기구 수납",
    "T-14": "실제 드라이브·모터 운전",
}
ALL_IDS = list(T_TITLES)
SUBSTITUTED_IDS = {
    "T-01": "기존 실적으로 대체(D-028): 100/200/500 kHz·1 MHz 외부 측정, 1 MHz 운전. 근거: servo1axis CURRENT_STATE.md",
    "T-02": "기존 실적으로 대체(D-028): 200,000 펄스 x3(200 kHz), 1,000,000 펄스(1 MHz) 루프백 일치. 근거: servo1axis CURRENT_STATE.md",
}
HARDWARE_ONLY = {"T-10", "T-11", "T-12", "T-13"}


def judge(tid: str, d: dict) -> dict:
    """d = measured data of one run. Returns {status, reasons[]}."""
    why = []
    if tid == "T-03":
        fmin = d.get("la_f_min_hz")
        if fmin is None:
            return {"status": NOT_RUN, "reasons": ["로직 분석기 파일 없음"]}
        step = d.get("freq_hz", 0)
        tol = d.get("tol_pct", 0.5) / 100.0        # [assumed] analyser timing tolerance
        if step == 200_000:
            ok = fmin >= 200_000 * (1 - tol)
            return {"status": PASS if ok else FAIL,
                    "reasons": [f"두 축 최저 측정 {fmin:.0f} Hz (기준 >= 200 kHz, 측정 허용오차 {tol*100:.1f}% [가정])"]}
        return {"status": RECORD, "reasons": [f"{step} Hz 단계는 기록 항목, 측정 최저 {fmin:.0f} Hz"]}
    if tid == "T-04":
        bad = d.get("err_ax1", 0) + d.get("err_ax2", 0) + d.get("overshoot_ax1", 0) + d.get("overshoot_ax2", 0)
        ext = d.get("external_count_matches")
        if bad:
            why.append(f"PCNT 불일치/초과 합 {bad}")
        if ext is False:
            why.append("외부 계수 != 목표")
        if ext is None:
            why.append("외부 계수 미입력(PCNT만 판정)")
        return {"status": FAIL if (bad or ext is False) else PASS, "reasons": why}
    if tid == "T-05":
        bad = d.get("err_ax1", 0) + d.get("err_ax2", 0)
        late = d.get("upd_late", 0)
        if bad:
            why.append(f"펄스 오차 {bad}")
        if late:
            why.append(f"갱신 지연 {late}건")
        why.append("응답 시간 분포는 기록 항목")
        return {"status": FAIL if (bad or late) else PASS, "reasons": why}
    if tid == "T-06":
        bad = d.get("err_ax1", 0) + d.get("err_ax2", 0)
        ovs = d.get("overshoot_ax1", 0) + d.get("overshoot_ax2", 0)
        iram_on = d.get("iram_safe")
        if iram_on:
            return {"status": PASS if not (bad or ovs) else FAIL, "reasons": [f"IRAM 안전 on: 오차 {bad}, 초과 {ovs}"]}
        return {"status": RECORD, "reasons": [f"IRAM 안전 off(비교용): 오차 {bad}, 초과 {ovs}"]}
    if tid == "T-07":
        relaunch = d.get("relaunch_after_latch", 0)
        why.append("정지 지연·경로 간 계수 차이는 기록 항목(NFR-011 미확인)")
        if relaunch:
            return {"status": FAIL, "reasons": why + [f"래치 중 재기동 {relaunch}"]}
        return {"status": RECORD, "reasons": why + ["래치 중 재기동 0"]}
    if tid == "T-08":
        ovs = d.get("overshoot_ax1", 0) + d.get("overshoot_ax2", 0)
        return {"status": FAIL if ovs else RECORD, "reasons": [f"최악 지연 기록; 설정 start_speed에서 초과 펄스 {ovs}"]}
    if tid == "T-09":
        over = d.get("requests_exceeded", [])
        if over:
            return {"status": FAIL, "reasons": [f"요청 초과 출력: {over}"]}
        return {"status": PASS if d.get("freq_checked") else NOT_RUN, "reasons": ["요청 초과 출력 0" if d.get("freq_checked") else "측정 없음"]}
    if tid == "T-14":
        return {"status": RECORD, "reasons": ["1축은 기존 실적으로 대체(D-028); 2축 드라이브 수신 펄스 대조는 사용자 입력"]}
    if tid in HARDWARE_ONLY:
        return {"status": RECORD if d else NOT_RUN, "reasons": ["하드웨어 시제 필요 항목"]}
    return {"status": RECORD, "reasons": []}
