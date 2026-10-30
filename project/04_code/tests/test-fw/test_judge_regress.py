"""Regression tests for review round 1 (test-fw-review-1): C-01 .. C-03, M-06, M-07, M-09, m-05, m-06.
Each test fails on the pre-review judge/scenario code (false PASS) and passes now."""
import json
import os
import socket

import pytest

from tfw_host import regs
from tfw_host.judge import judge, PASS, FAIL, NOT_RUN, RECORD
from tfw_host.mock import MockTfwServer
from tfw_host.modbus import ModbusTcpClient
from tfw_host.scenario import run_scenario, analyse_la, probe_latched_run, run_step


def base(tid):
    """A complete, genuinely good run for each T-ID."""
    d = dict(wait_status=2, timed_out=False, seq_done=1000, err_ax1=0, err_ax2=0, overshoot_ax1=0, overshoot_ax2=0,
             cmd="RUN_SEQ", repeat=1000, axis_mask=3, param_status=2, param_mismatch=[], clear_ok=True, upd_late=0,
             la_edges=[2000, 2000], la_expected_edges=[2000, 2000], freq_hz=200000, seed_changed=False,
             la_fresh=True, la_fresh_reasons=[])                      # R2 N-04: the CSV is proven to be this run's capture
    if tid == "T-03":
        d.update(cmd="RUN_FIXED", repeat=1, seq_done=1, la_f_avg_hz=[200000.0, 200000.0], la_f_min_hz=199000.0, la_edges=[9, 9])
    if tid == "T-05":
        d.update(tcp_load={"refused": 0, "requests": 500, "errors": 0, "dropped": 0, "idle_clients": 0}, tcp_clients=4, hmi=True,
                 mb_clients_max=6, mb_clients_min=6, mb_max_clients=8)
    if tid == "T-06":
        d.update(nvs_period_ms=100, nvs_writes=100, run_s=20.0, cfg_flags=1, iram_safe=True)
    if tid == "T-09":
        d.update(cmd="RUN_FIXED", repeat=1, seq_done=1, freq_checked=True, requests_exceeded=[], la_f_max_hz=200000.0)
    return d


GOOD = {"T-03": PASS, "T-04": PASS, "T-05": PASS, "T-06": PASS, "T-08": RECORD, "T-09": PASS}
RUN_TIDS = list(GOOD)


@pytest.mark.parametrize("tid", RUN_TIDS)
def test_base_runs_are_good(tid):
    assert judge(tid, base(tid))["status"] == GOOD[tid], judge(tid, base(tid))


# ---------------------------------------------------------------- C-01
@pytest.mark.parametrize("tid", RUN_TIDS)
@pytest.mark.parametrize("ws,why", [(0x10, "거부"), (0x11, "범위"), (0x12, "자원"), (0, "중단"), (None, "타임아웃")])
def test_c01_run_that_did_not_complete_is_fail(tid, ws, why):
    d = base(tid)
    d["wait_status"] = ws                 # all counters stay 0: the old judge said PASS
    v = judge(tid, d)
    assert v["status"] == FAIL, v
    assert any(why in r or "DONE" in r for r in v["reasons"])


@pytest.mark.parametrize("tid", RUN_TIDS)
def test_c01_timeout_flag_is_fail(tid):
    d = base(tid)
    d["timed_out"] = True
    assert judge(tid, d)["status"] == FAIL


@pytest.mark.parametrize("tid", ["T-03", "T-04", "T-05", "T-06", "T-09"])
def test_c01_seq_done_short_is_fail(tid):
    d = base(tid)
    d["seq_done"] = d["seq_done"] - 1
    assert judge(tid, d)["status"] == FAIL


@pytest.mark.parametrize("tid", RUN_TIDS)
@pytest.mark.parametrize("key", ["wait_status", "timed_out", "seq_done", "err_ax1", "err_ax2", "overshoot_ax1", "overshoot_ax2",
                                 "cmd", "repeat", "axis_mask", "param_status", "param_mismatch", "clear_ok"])
def test_c01_missing_key_never_passes(tid, key):
    d = base(tid)
    del d[key]                            # old code: d.get(key, 0) -> PASS
    assert judge(tid, d)["status"] == NOT_RUN


@pytest.mark.parametrize("tid", RUN_TIDS)
def test_c01_parameter_not_applied_is_fail(tid):
    d = base(tid)
    assert judge(tid, dict(d, param_status=0x11))["status"] == FAIL
    assert judge(tid, dict(d, param_mismatch=["FREQ: wrote 50, read 100000"]))["status"] == FAIL
    assert judge(tid, dict(d, clear_ok=False))["status"] == FAIL
    assert judge(tid, dict(d, seed_changed=True))["status"] == FAIL


def test_c01_t04_needs_two_axes_and_1000_runs():
    assert judge("T-04", dict(base("T-04"), axis_mask=1, la_edges=[2000, 0], la_expected_edges=[2000, 0]))["status"] == NOT_RUN
    d = dict(base("T-04"), repeat=999, seq_done=999)
    assert judge("T-04", d)["status"] == NOT_RUN


def _steps_file(tmp_path, step):
    sc = tmp_path / "sc.json"
    sc.write_text(json.dumps({"steps": [step]}))
    return str(sc)


def _csv(path, n1, n2, span=30.0):
    with open(path, "w") as f:
        f.write("Time [s],ax1,ax2\n0.0,0,0\n")
        for i in range(max(n1, n2)):
            t = 1e-5 * (i + 1)
            a, b = int(i < n1), int(i < n2)
            f.write(f"{t:.7f},{a},{b}\n{t + 5e-6:.7f},0,0\n")
        f.write(f"{span},0,0\n")                    # capture end (no edge): the capture is longer than any mock run


def _cap(tmp_path, n1=2000, n2=2000):
    """The operator's export: called by run_scenario AFTER the run, so the file is newer than the run end."""
    def hook(step, rec):
        p = tmp_path / "la.csv"
        _csv(p, n1, n2)
        return str(p)
    return hook


T04 = {"id": "T-04", "cmd": "RUN_SEQ", "axis_mask": 3, "freq_hz": 200000, "count": 2, "repeat": 1000, "seed": 0,
       "timeout_s": 3, "la_csv": "la.csv"}


def _e2e(tmp_path, step, csv=(2000, 2000), **mock_kw):
    srv = MockTfwServer(**mock_kw).start()
    try:
        res = run_scenario(_steps_file(tmp_path, step), f"127.0.0.1:{srv.port}", str(tmp_path / "res"), capture=_cap(tmp_path, *csv))
        return res[step["id"]][0], srv
    finally:
        srv.stop()


def test_e2e_good_t04_passes_with_external_count(tmp_path):
    rec, _ = _e2e(tmp_path, T04)
    assert rec["verdict"]["status"] == PASS, rec["verdict"]
    assert rec["data"]["la_expected_edges"] == [2000, 2000]


def test_e2e_t04_lost_pulse_seen_only_externally(tmp_path):
    rec, _ = _e2e(tmp_path, T04, csv=(2000, 1999))          # PCNT counters are 0 (loop-back cannot see it)
    assert rec["data"]["err_ax2"] == 0
    assert rec["verdict"]["status"] == FAIL


def test_e2e_t04_rejected_run_is_fail(tmp_path):
    for st, mode in ((0x10, "normal"), (0x12, "normal")):
        rec, _ = _e2e(tmp_path, T04, reject_status=st)
        assert rec["verdict"]["status"] == FAIL, (st, rec["verdict"])
        assert rec["data"]["wait_status"] == st


def test_e2e_t04_aborted_run_is_fail(tmp_path):
    rec, _ = _e2e(tmp_path, T04, mode="abort")
    assert rec["data"]["wait_status"] == 0 and rec["data"]["err_ax1"] == 0
    assert rec["verdict"]["status"] == FAIL


def test_e2e_t04_timeout_is_fail(tmp_path):
    rec, _ = _e2e(tmp_path, dict(T04, timeout_s=0.3), mode="hang")
    assert rec["data"]["timed_out"] is True
    assert rec["verdict"]["status"] == FAIL


def test_e2e_t04_out_of_range_parameter_is_fail(tmp_path):
    rec, _ = _e2e(tmp_path, dict(T04, freq_hz=50))          # device ignores 50 Hz (<100) and sets 0x11
    assert rec["data"]["param_status"] == 0x11
    assert rec["verdict"]["status"] == FAIL


def test_e2e_t04_without_external_count_is_not_pass(tmp_path):
    step = {k: v for k, v in T04.items() if k != "la_csv"}
    rec, _ = _e2e(tmp_path, step)
    assert rec["verdict"]["status"] == NOT_RUN
    assert any("외부 계수" in r for r in rec["verdict"]["reasons"])


def test_e2e_t04_counter_error_still_fails_without_external(tmp_path):
    step = {k: v for k, v in T04.items() if k != "la_csv"}
    rec, _ = _e2e(tmp_path, step, inject={"ERR_AX1": 1})
    assert rec["verdict"]["status"] == FAIL


# ---------------------------------------------------------------- C-02
def test_c02_missing_external_is_not_run_not_pass():
    d = base("T-04")
    for k in ("la_edges", "la_expected_edges"):
        e = dict(d)
        del e[k]
        assert judge("T-04", e)["status"] == NOT_RUN
    assert judge("T-04", dict(d, la_expected_edges=[2000, None]))["status"] == NOT_RUN
    for tid in ("T-05", "T-06"):
        e = base(tid)
        del e["la_edges"]
        assert judge(tid, e)["status"] == NOT_RUN


def test_c02_per_axis_expectation_random_sequence(tmp_path):
    csvp = tmp_path / "la.csv"
    _csv(csvp, 5, 5)
    step = {"cmd": "RUN_SEQ", "axis_mask": 3, "freq_hz": 200000, "seed": 12345, "repeat": 1000, "la_csv": str(csvp)}
    data = {}
    analyse_la(step, data)
    e1, e2 = data["la_expected_edges"]
    assert e1 != e2 and e1 > 1000 and e2 > 1000          # the old code compared both axes with one number
    assert data["external_count_matches"] is False
    # an explicit per-axis list / a scalar are still honoured
    d2 = {}
    analyse_la(dict(step, la_expected_edges=[5, 5]), d2)
    assert d2["external_count_matches"] is True
    d3 = {}
    analyse_la(dict(step, la_expected_edges=5), d3)
    assert d3["la_expected_edges"] == [5, 5]


# ---------------------------------------------------------------- C-03
def test_c03_t05_refused_connections_fail():
    d = base("T-05")
    d["tcp_load"] = {"refused": 1, "requests": 500, "errors": 1}
    v = judge("T-05", d)
    assert v["status"] == FAIL and any("거부" in r for r in v["reasons"])


def test_c03_t05_no_requests_or_no_load_or_too_few_connections():
    assert judge("T-05", dict(base("T-05"), tcp_load={"refused": 0, "requests": 0, "errors": 0}))["status"] == FAIL
    d = base("T-05")
    del d["tcp_load"]
    assert judge("T-05", d)["status"] != PASS
    assert judge("T-05", dict(base("T-05"), mb_clients_max=4))["status"] == FAIL
    assert judge("T-05", dict(base("T-05"), mb_clients_max=None))["status"] == NOT_RUN


def test_c03_t05_default_build_max_clients_4_cannot_pass():
    """Design contradiction (6 connections vs max 4): the judge must not turn it into a pass."""
    d = dict(base("T-05"), mb_max_clients=4, mb_clients_max=4)
    v = judge("T-05", d)
    assert v["status"] == FAIL and any("max_clients" in r for r in v["reasons"])


def test_c03_t05_needs_four_clients_and_hmi():
    assert judge("T-05", dict(base("T-05"), tcp_clients=2, mb_clients_max=4))["status"] == NOT_RUN
    assert judge("T-05", dict(base("T-05"), hmi=False))["status"] == NOT_RUN


def test_c03_t05_late_update_or_pulse_error_fails():
    assert judge("T-05", dict(base("T-05"), upd_late=1))["status"] == FAIL
    assert judge("T-05", dict(base("T-05"), err_ax2=1))["status"] == FAIL


def test_c03_t06_no_nvs_writes_is_fail():
    v = judge("T-06", dict(base("T-06"), nvs_writes=0))
    assert v["status"] == FAIL and any("NVS" in r for r in v["reasons"])
    assert judge("T-06", dict(base("T-06"), nvs_period_ms=0))["status"] == NOT_RUN
    assert judge("T-06", dict(base("T-06"), nvs_writes=9))["status"] == FAIL      # need int(20*1000/100*0.5)=100


def test_c03_t06_iram_flag_comes_from_device():
    # scenario says on, device build says off -> the result of an "off" build must not be booked as "on passed"
    v = judge("T-06", dict(base("T-06"), cfg_flags=0, iram_safe=True))
    assert v["status"] == FAIL
    assert judge("T-06", dict(base("T-06"), cfg_flags=0, iram_safe=False))["status"] == RECORD
    assert judge("T-06", dict(base("T-06"), cfg_flags=0, iram_safe=None, overshoot_ax1=3))["status"] == RECORD
    assert judge("T-06", dict(base("T-06"), cfg_flags=1, iram_safe=False))["status"] == FAIL


T05 = {"id": "T-05", "cmd": "RUN_SEQ", "axis_mask": 3, "freq_hz": 200000, "count": 2, "repeat": 1000, "seed": 0,
       "timeout_s": 5, "la_csv": "la.csv", "hmi": True, "tcp_load": {"clients": 4, "fc": "MIX", "qty": 125, "interval_ms": 1}}


def _hmi(srv):
    c = ModbusTcpClient("127.0.0.1", srv.port, 1).connect()
    c.read_holding(0x7000, 1)
    return c


def _e2e_hmi(tmp_path, step, max_clients, run_time=0.6, **kw):
    srv = MockTfwServer(max_clients=max_clients, run_time_s=run_time, **kw).start()
    hmi = _hmi(srv)
    try:
        res = run_scenario(_steps_file(tmp_path, step), f"127.0.0.1:{srv.port}", str(tmp_path / "res"), capture=_cap(tmp_path))
        return res[step["id"]][0], srv
    finally:
        hmi.close()
        srv.stop()


def test_e2e_t05_default_max_clients_4_is_fail_not_pass(tmp_path):
    rec, _ = _e2e_hmi(tmp_path, T05, max_clients=4)
    assert rec["data"]["tcp_load"]["refused"] > 0
    assert rec["verdict"]["status"] == FAIL, rec["verdict"]


def test_e2e_t05_with_enough_connections_passes(tmp_path):
    rec, srv = _e2e_hmi(tmp_path, T05, max_clients=8)
    assert rec["data"]["mb_clients_max"] >= 6 and rec["data"]["mb_max_clients"] == 8
    assert rec["data"]["tcp_load"]["refused"] == 0 and rec["data"]["tcp_load"]["requests"] > 0
    assert rec["verdict"]["status"] == PASS, rec["verdict"]


def test_e2e_t06_nvs_load_must_run(tmp_path):
    step = {"id": "T-06", "cmd": "RUN_SEQ", "axis_mask": 3, "freq_hz": 200000, "count": 2, "repeat": 1000, "seed": 0,
            "timeout_s": 5, "la_csv": "la.csv", "nvs_period_ms": 10, "iram_safe": True}
    rec, _ = _e2e(tmp_path, step, run_time_s=0.3)
    assert rec["data"]["nvs_writes"] >= 5 and rec["data"]["cfg_flags"] & 1
    assert rec["verdict"]["status"] == PASS, rec["verdict"]
    rec, _ = _e2e(tmp_path, dict(step, iram_safe=True), run_time_s=0.3, iram_flag=False)     # device build is "off"
    assert rec["verdict"]["status"] == FAIL


# ---------------------------------------------------------------- M-07
def test_m07_tcp_load_never_changes_seed(tmp_path):
    step = dict(T05, seed=777, count=2)
    rec, srv = _e2e_hmi(tmp_path, step, max_clients=8)
    assert regs.join_u32(srv.regs[regs.R["SEED"]], srv.regs[regs.R["SEED"] + 1]) == 777
    assert rec["data"]["seed_changed"] is False


def test_m07_seed_change_during_run_is_fail():
    assert judge("T-05", dict(base("T-05"), seed_changed=True))["status"] == FAIL


# ---------------------------------------------------------------- M-09
class FakeDev:
    def __init__(self, es_state, refuses):
        self.es, self.refuses, self.status, self.seq = es_state, refuses, regs.STATUS["IDLE"], 0

    def r16(self, name):
        return {"ES_STATE": self.es, "STATUS": self.status}[name]

    def r32(self, name):
        return self.seq

    def w16(self, name, v):
        if name == "CMD":
            if self.refuses:
                self.status = regs.STATUS["REJECT_BUSY"]
            else:
                self.status = regs.STATUS["RUNNING"]


def test_m09_latch_probe_results():
    assert probe_latched_run(FakeDev(2, True), wait_s=0.01) == 0
    assert probe_latched_run(FakeDev(1, True), wait_s=0.01) == 0
    assert probe_latched_run(FakeDev(2, False), wait_s=0.01) == 1       # started although latched -> failure
    assert probe_latched_run(FakeDev(0, True), wait_s=0.01) is None     # not latched: not measurable


T07 = dict(es_last_err=0, es_trip_count_before=0, es_trip_count=1)        # R2 N-02: the preconditions of a T-07 result


def test_m09_t07_unmeasured_is_stated_as_unmeasured():
    v = judge("T-07", dict(T07))
    assert v["status"] == RECORD and any("미측정" in r for r in v["reasons"])
    assert not any("재기동 0" in r and "미측정" not in r for r in v["reasons"])      # old text claimed "래치 중 재기동 0"
    assert judge("T-07", dict(T07, relaunch_after_latch=1))["status"] == FAIL
    assert judge("T-07", dict(T07, relaunch_after_latch=0))["status"] == RECORD


def test_m09_e2e_latch_probe_with_mock(tmp_path):
    srv = MockTfwServer(trip_on_run=True).start()                          # the trip is counted while the run is attempted (R2 N-02)
    try:
        srv.regs[regs.R["ES_STATE"]] = 2                                   # latched
        step = {"id": "T-07", "cmd": "RUN_FIXED", "axis_mask": 1, "freq_hz": 200000, "count": 10, "timeout_s": 2, "latch_probe": True}
        # first run is refused by the latch itself (0x10), then the probe confirms the refusal
        res = run_scenario(_steps_file(tmp_path, step), f"127.0.0.1:{srv.port}", str(tmp_path / "r"))
        rec = res["T-07"][0]
        assert rec["data"]["relaunch_after_latch"] == 0
        assert rec["verdict"]["status"] == RECORD
    finally:
        srv.stop()


# ---------------------------------------------------------------- m-05 / m-06
def test_m05_t03_uses_average_frequency_not_a_loose_tolerance():
    d = base("T-03")
    assert judge("T-03", dict(d, la_f_avg_hz=[199500.0, 200000.0]))["status"] == FAIL      # was PASS with 0.5 %
    assert judge("T-03", dict(d, la_f_avg_hz=[200000.0, 200010.0]))["status"] == PASS
    assert judge("T-03", dict(d, la_f_avg_hz=[200000.0, 200000.0], la_edges=[9, 0]))["status"] == NOT_RUN
    assert judge("T-03", {k: v for k, v in d.items() if k != "la_f_avg_hz"})["status"] == NOT_RUN


def test_m05_t03_rejected_run_with_stale_capture_is_fail():
    assert judge("T-03", dict(base("T-03"), wait_status=0x12))["status"] == FAIL


def test_t09_without_pulses_in_capture_is_not_pass():
    assert judge("T-09", dict(base("T-09"), la_f_max_hz=0.0))["status"] == NOT_RUN
    assert judge("T-09", {k: v for k, v in base("T-09").items() if k != "freq_checked"})["status"] == NOT_RUN


def test_m06_trigger_edge_must_match_polarity(tmp_path):
    p = tmp_path / "t.csv"
    with open(p, "w") as f:
        f.write("Time [s],trg,ax1,ax2\n0.0,1,0,0\n0.001,0,1,0\n0.0011,0,0,0\n")
    d = dict(T07, la_fresh=True)
    analyse_la({"la_csv": str(p), "la_trigger_channel": "trg", "estop_active_low": False, "la_trigger_edge": "fall"}, d)
    assert d["la_trigger_edge_mismatch"] is True          # active-HIGH build must trigger on the rising edge
    assert judge("T-07", d)["status"] == NOT_RUN
    d2 = {}
    analyse_la({"la_csv": str(p), "la_trigger_channel": "trg", "estop_active_low": True, "la_trigger_edge": "fall"}, d2)
    assert d2["la_trigger_edge_mismatch"] is False
