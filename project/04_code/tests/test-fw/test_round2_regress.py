"""Regression tests for review round 2 (test-fw-review-2): N-04, N-01, N-02, N-03, M-05, R-01 and some Minor items.
Each test fails on the round 2 code (verified, see notes section 8) and passes now."""
import json
import os
import re
import time

import pytest

from conftest import ROOT
from tfw_host import regs
from tfw_host.judge import judge, PASS, FAIL, NOT_RUN, RECORD
from tfw_host.la import first_periods
from tfw_host.load import start_load
from tfw_host.mock import MockTfwServer
from tfw_host.scenario import run_scenario, judge_file, analyse_la, capture_check
from test_judge_regress import base, _csv, _cap, _steps_file, T04, T05, _e2e, _e2e_hmi, T07


def src(*p):
    with open(os.path.join(ROOT, *p), encoding="utf-8") as f:
        return f.read()


# ================================================================ N-04: the CSV must be THIS run's capture
@pytest.mark.parametrize("tid", ["T-03", "T-04", "T-05", "T-06", "T-09"])
def test_n04_la_data_without_freshness_proof_is_fail(tid):
    d = base(tid)
    del d["la_fresh"]
    assert judge(tid, d)["status"] == FAIL
    assert judge(tid, dict(d, la_fresh=False, la_fresh_reasons=["old"]))["status"] == FAIL


def test_n04_csv_made_before_the_run_never_passes(tmp_path):
    """The round 2 flow: the CSV of an earlier capture is lying around, the run happens, the CSV is read at once."""
    _csv(tmp_path / "la.csv", 2000, 2000)                      # earlier capture, correct edge counts
    old = time.time() - 600
    os.utime(tmp_path / "la.csv", (old, old))
    srv = MockTfwServer().start()
    try:
        res = run_scenario(_steps_file(tmp_path, T04), f"127.0.0.1:{srv.port}", str(tmp_path / "res"))     # no capture hook
        rec = res["T-04"][0]
        assert rec["verdict"]["status"] == "PENDING", rec["verdict"]                                         # not judged yet
        res = run_scenario(_steps_file(tmp_path, T04), f"127.0.0.1:{srv.port}", str(tmp_path / "res2"),
                           capture=lambda s, r: str(tmp_path / "la.csv"))                                 # operator points at the old file
        rec = res["T-04"][0]
        assert rec["verdict"]["status"] == FAIL and any("이전" in x or "묵은" in x for x in rec["verdict"]["reasons"]), rec["verdict"]
        assert rec["data"]["la_fresh"] is False
    finally:
        srv.stop()


def test_n04_same_csv_for_two_runs_is_fail(tmp_path):
    srv = MockTfwServer().start()
    sc = tmp_path / "sc.json"
    sc.write_text(json.dumps({"steps": [dict(T04, name="a"), dict(T04, name="b")]}))
    try:
        res = run_scenario(str(sc), f"127.0.0.1:{srv.port}", str(tmp_path / "res"), capture=_cap(tmp_path))
        a, b = res["T-04"]
        assert a["verdict"]["status"] == PASS, a["verdict"]
        assert b["verdict"]["status"] == FAIL and any("sha256" in x for x in b["verdict"]["reasons"]), b["verdict"]
    finally:
        srv.stop()


def test_n04_capture_shorter_than_the_run_is_fail(tmp_path):
    srv = MockTfwServer(run_time_s=2.0).start()
    try:
        def short(step, rec):
            _csv(tmp_path / "la.csv", 2000, 2000, span=0.5)
            return str(tmp_path / "la.csv")
        res = run_scenario(_steps_file(tmp_path, dict(T04, timeout_s=10)), f"127.0.0.1:{srv.port}", str(tmp_path / "res"), capture=short)
        v = res["T-04"][0]["verdict"]
        assert v["status"] == FAIL and any("캡처 길이" in x for x in v["reasons"]), v
    finally:
        srv.stop()


def test_n04_run_then_judge_command_attaches_the_capture(tmp_path):
    """`run` leaves PENDING; `judge <json> <csv>` with a capture exported afterwards decides."""
    srv = MockTfwServer().start()
    try:
        res = run_scenario(_steps_file(tmp_path, T04), f"127.0.0.1:{srv.port}", str(tmp_path / "res"))
    finally:
        srv.stop()
    jf = next(p for p in (tmp_path / "res").iterdir() if p.name.startswith("T-04"))
    assert json.loads(jf.read_text())["verdict"]["status"] == "PENDING"
    assert json.loads(jf.read_text())["data"]["run_end_ts"] > json.loads(jf.read_text())["data"]["run_start_ts"]
    time.sleep(0.05)
    _csv(tmp_path / "late.csv", 2000, 1999)                      # exported after the run: one pulse missing on axis 2
    rec = judge_file(str(jf), str(tmp_path / "late.csv"))
    assert rec["verdict"]["status"] == FAIL and json.loads(jf.read_text())["verdict"]["status"] == FAIL
    _csv(tmp_path / "late2.csv", 2000, 2000)
    rec = judge_file(str(jf), str(tmp_path / "late2.csv"))        # re-judging the same record with another capture
    assert rec["verdict"]["status"] == PASS, rec["verdict"]


def test_n04_cli_has_judge_command():
    from tfw_host import cli
    with pytest.raises(SystemExit):
        cli.main(["judge"])                                       # missing arguments: the sub-command exists
    assert "judge" in src("host", "tfw_host", "cli.py")


# ================================================================ N-01: bool options only through helper macros
def _c_files():
    for d, _, fs in os.walk(ROOT):
        if "host" in os.path.relpath(d, ROOT).split(os.sep)[:1]:
            continue
        for f in fs:
            if f.endswith((".c", ".h")):
                yield os.path.join(d, f)


def _bool_options():
    k = src("main", "Kconfig.projbuild")
    names = set(re.findall(r"config (\w+)\n\s+bool", k))
    for frag in ("sdkconfig.tfw_iram_on", "sdkconfig.tfw_iram_off", "sdkconfig.defaults"):
        for line in src(frag).splitlines():
            m = re.match(r"#?\s*(?:CONFIG_)?(\w+?)(?:=y| is not set)$", line.strip())
            if m and line.strip().startswith(("CONFIG_", "# CONFIG_")):
                names.add(m.group(1))
    return {n for n in names if n.isupper()}


def test_n01_bool_config_macros_are_used_only_in_preprocessor_conditions():
    names = _bool_options()
    assert {"TFW_UPD_MODE_HWTIMER", "TFW_ESTOP_ACTIVE_LOW", "TFW_LEGACY_NVS_ON_WRITE", "PCNT_ISR_IRAM_SAFE"} <= names
    bad = []
    for path in _c_files():
        txt = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), open(path, encoding="utf-8").read(), flags=re.S)
        txt = re.sub(r"//[^\n]*", "", txt)
        # join continuation lines so that a multi-line #if is seen as one directive
        for n, line in enumerate(txt.splitlines(), 1):
            if re.match(r"\s*#\s*(if|ifdef|ifndef|elif)\b", line):
                continue
            for m in re.finditer(r"\bCONFIG_(\w+)", line):
                if m.group(1) in names:
                    bad.append(f"{os.path.relpath(path, ROOT)}:{n}: CONFIG_{m.group(1)}")
    assert not bad, bad


# ================================================================ N-02 (static part; behaviour: test_cfg_syntax.py host build)
def test_n02_init_failure_blocks_motion_and_main_reports_it():
    es = src("components", "tfw_estop_io", "tfw_estop_io.c")
    assert re.search(r"static volatile bool s_init_ok;", es)
    assert "s_init_ok = true;" in es and es.count("s_init_ok = true;") == 1       # set only at the end of a successful init
    main = src("main", "tfw_main.c")
    assert "ALL motion commands are refused" in main


# ================================================================ N-02 judge: T-07 preconditions
def test_n02_t07_needs_clean_init_and_a_recorded_trip():
    ok = dict(T07, relaunch_after_latch=0)
    assert judge("T-07", ok)["status"] == RECORD
    for k in ("es_last_err", "es_trip_count", "es_trip_count_before"):
        d = dict(ok)
        del d[k]
        assert judge("T-07", d)["status"] == NOT_RUN, k
    assert judge("T-07", dict(ok, es_last_err=0x103))["status"] == NOT_RUN                  # service already installed
    assert judge("T-07", dict(ok, es_trip_count=0))["status"] == NOT_RUN                    # no trip counted
    # a relaunch after the latch must still be reported when the preconditions hold
    assert judge("T-07", dict(ok, relaunch_after_latch=1))["status"] == FAIL


def test_n02_e2e_t07_with_init_error_in_device_is_not_run(tmp_path):
    srv = MockTfwServer(trip_on_run=True).start()
    try:
        srv.regs[regs.R["ES_STATE"]] = 2
        srv.regs[regs.R["ES_LAST_ERR"]] = 0x103
        step = {"id": "T-07", "cmd": "RUN_FIXED", "axis_mask": 1, "freq_hz": 200000, "count": 10, "timeout_s": 2, "latch_probe": True}
        res = run_scenario(_steps_file(tmp_path, step), f"127.0.0.1:{srv.port}", str(tmp_path / "r"))
        assert res["T-07"][0]["data"]["es_last_err"] == 0x103
        assert res["T-07"][0]["verdict"]["status"] == NOT_RUN
    finally:
        srv.stop()


# ================================================================ N-03 (static part of pulse2; sequencer: test_logic.c)
def test_n03_start_refused_and_rechecked_while_estop_is_latched():
    t = src("components", "tfw_pulse2", "tfw_pulse2.c")
    sc = t[t.index("static esp_err_t start_common"):]
    sc = sc[:sc.index("\nesp_err_t tfw_pulse2_move")]
    first = sc.index("s_block_fn()")                             # before pcnt_unit_clear_count
    assert first < sc.index("pcnt_unit_clear_count(a->pcnt)")
    start = sc.index("mcpwm_timer_start_stop(a->timer, MCPWM_TIMER_START_NO_STOP)")
    assert sc.index("s_block_fn()", start) > start                # and once more after the timer was started
    assert "tfw_pulse2_stop((uint8_t)(a - s_ax))" in sc
    assert "tfw_pulse2_set_block_fn(tfw_estop_blocking)" in src("main", "tfw_main.c")


def test_n03_hw_move_and_hw_fixed_refuse_during_block():
    main = src("main", "tfw_main.c")
    for fn in ("hw_move", "hw_fixed"):
        m = re.search(r"static int " + fn + r"\(.*?\n?\{(.*?)\n(?:static|\n)", main, re.S)
        assert m and "tfw_estop_blocking()" in m.group(1), fn


def test_n03_isr_latches_before_stopping():
    t = src("components", "tfw_estop_io", "tfw_estop_io.c")
    body = t[t.index("es_isr(void *arg)"):]
    body = body[:body.index("\n}")]
    assert body.index("tfw_es_trip(") < body.index("tfw_pulse2_stop(0)")


# ================================================================ M-05: first periods after a stop
def _period_csv(path, periods_us, ch="ax1"):
    t = 1.0
    with open(path, "w") as f:
        f.write(f"Time [s],{ch}\n0.0,0\n")
        for p in periods_us:
            f.write(f"{t:.9f},1\n{t + p * 0.5e-6:.9f},0\n")
            t += p * 1e-6
        f.write(f"{t:.9f},1\n{t + 1e-5:.9f},0\n{t + 5.0:.9f},0\n")


T9 = {"cmd": "RUN_FIXED", "axis_mask": 1, "freq_hz": 5000, "la_pulse_channels": ["ax1"], "check_not_above_request": True,
      "la_sample_rate_hz": 50_000_000, "la_first_periods": {"n": 3, "res_hz": 10_000_000}}


def _t09(tmp_path, periods):
    p = tmp_path / "f.csv"
    _period_csv(p, periods)
    d = {}
    analyse_la(dict(T9, la_csv=str(p)), d)
    return dict(base("T-09"), **d)


def test_m05_first_periods_ok_when_equal_to_the_commanded_period(tmp_path):
    assert first_periods(str(_mk(tmp_path, [200.0] * 6)), "ax1", 3) == pytest.approx([200.0] * 3, rel=1e-6)
    v = judge("T-09", _t09(tmp_path, [200.0] * 8))
    assert v["status"] == PASS, v


def _mk(tmp_path, per):
    p = tmp_path / "x.csv"
    _period_csv(p, per)
    return p


def test_m05_shorter_first_period_is_fail(tmp_path):
    """1 MHz -> 5 kHz: the first period still has the old 1 us (or any value below the commanded 200 us)."""
    v = judge("T-09", _t09(tmp_path, [1.0, 200.0, 200.0, 200.0, 200.0]))
    assert v["status"] == FAIL and any("짧음" in r for r in v["reasons"]), v


def test_m05_longer_first_period_is_fail_the_case_round2_could_not_detect(tmp_path):
    """5 kHz -> 1 MHz: the first period is 200 us instead of 1 us, i.e. LOWER than requested: round 2 saw no error."""
    d = _t09(tmp_path, [200.0, 1.0, 1.0, 1.0, 1.0])
    d2 = {}
    p = tmp_path / "g.csv"
    _period_csv(p, [200.0, 1.0, 1.0, 1.0, 1.0])
    analyse_la(dict(T9, freq_hz=1_000_000, la_csv=str(p), la_first_periods={"n": 3, "res_hz": 10_000_000}), d2)
    v = judge("T-09", dict(base("T-09"), **d2))
    assert v["status"] == FAIL and any("길음" in r for r in v["reasons"]), v
    assert d2["requests_exceeded"] == []                       # the old rule (request exceeded) really sees nothing here


def test_m05_too_few_edges_is_not_run(tmp_path):
    v = judge("T-09", _t09(tmp_path, [200.0, 200.0]))
    assert v["status"] == NOT_RUN


def test_m05_example_scenario_has_the_1mhz_step_before_5khz():
    import yaml
    steps = yaml.safe_load(open(os.path.join(ROOT, "host", "scenario_example.yaml"), encoding="utf-8"))["steps"]
    t9 = [s for s in steps if s["id"] == "T-09"]
    assert [s["freq_hz"] for s in t9] == [1_000_000, 5000, 1_000_000]
    assert t9[1]["la_first_periods"]["n"] >= 3 and t9[2]["la_first_periods"]["n"] >= 3


# ================================================================ R-01: the load must last for the whole run
def test_r01_minimum_connections_during_the_run_is_required():
    assert judge("T-05", dict(base("T-05"), mb_clients_min=3))["status"] == FAIL         # peak was 6, then clients dropped
    d = base("T-05")
    del d["mb_clients_min"]
    assert judge("T-05", d)["status"] == NOT_RUN
    v = judge("T-05", dict(base("T-05"), tcp_load={"refused": 0, "requests": 500, "errors": 1, "dropped": 1, "idle_clients": 0}))
    assert v["status"] == FAIL and any("끊김" in r for r in v["reasons"])
    v = judge("T-05", dict(base("T-05"), tcp_load={"refused": 0, "requests": 500, "errors": 0, "dropped": 0, "idle_clients": 1}))
    assert v["status"] == FAIL and any("한 건도" in r for r in v["reasons"])
    assert judge("T-05", base("T-05"))["status"] == PASS


def test_r01_load_client_that_loses_its_connection_is_counted_as_dropped():
    import socket
    import threading
    ls = socket.socket()
    ls.bind(("127.0.0.1", 0))
    ls.listen(4)

    def serve():
        c, _ = ls.accept()
        req = c.recv(12)                                                    # the SEED read of the load client
        c.sendall(req[:4] + b"\x00\x07" + req[6:8] + b"\x04\x00\x00\x00\x00")
        time.sleep(0.2)
        c.close()                                                           # then the connection is lost mid-run
    threading.Thread(target=serve, daemon=True).start()
    run = start_load(f"127.0.0.1:{ls.getsockname()[1]}", 1, "FC03", 10, 1)
    time.sleep(0.8)
    st = run.stop()
    ls.close()
    assert st.dropped == 1 and st.summary()["dropped"] == 1


def test_r01_e2e_minimum_is_measured_with_a_dropping_hmi(tmp_path):
    step = dict(T05, tcp_load=dict(T05["tcp_load"], settle_s=0.0))
    rec, srv = _e2e_hmi(tmp_path, step, max_clients=8, run_time=0.8)
    assert rec["data"]["mb_clients_min"] is not None and rec["data"]["mb_clients_min"] >= 6
    assert rec["verdict"]["status"] == PASS, rec["verdict"]


# ================================================================ n-2 (load): no write-back without a first read
def test_n2_first_seed_read_timeout_does_not_write_zero():
    srv = MockTfwServer(max_clients=8, delay_s=2.5).start()       # every answer later than the client timeout (2 s)
    try:
        before = (srv.regs.get(regs.R["SEED"]), srv.regs.get(regs.R["SEED"] + 1))
        run = start_load(f"127.0.0.1:{srv.port}", 1, "FC16", 2, 0)
        time.sleep(2.6)
        st = run.stop()
        assert (srv.regs.get(regs.R["SEED"]), srv.regs.get(regs.R["SEED"] + 1)) == before
        assert st.requests == 0
    finally:
        srv.stop()


# ================================================================ n-1: tolerance from the analyser resolution
def test_n1_exceed_limit_comes_from_the_sample_rate(tmp_path):
    # 3 kHz commanded with 10 MHz res -> 3334 ticks (ceil) = 2999.4 Hz; the old floor rule gave 3333 ticks = 3000.3 Hz
    p = tmp_path / "f.csv"
    _period_csv(p, [1e6 / 3000.3] * 6)
    d = {}
    analyse_la({"la_csv": str(p), "freq_hz": 3000, "check_not_above_request": True, "la_pulse_channels": ["ax1"],
                "la_sample_rate_hz": 1_000_000_000, "tol_pct": 0.5}, d)
    assert d["requests_exceeded"] and "LA" in d["freq_tol_basis"]       # the 0.5 % guess would have hidden 0.01 % over-run
    d2 = {}
    analyse_la({"la_csv": str(p), "freq_hz": 3000, "check_not_above_request": True, "la_pulse_channels": ["ax1"], "tol_pct": 0.5}, d2)
    assert d2["requests_exceeded"] == []


# ================================================================ n-3 / n-8
def test_n3_check_script_lists_the_timer_stop_called_from_the_estop_isr():
    assert "mcpwm_timer_start_stop" in src("tools", "check_iram.sh").split("echo \"--- call targets")[0]


def test_n8_clear_stats_does_not_write_the_update_statistics_from_the_modbus_task():
    t = src("components", "tfw_pulse2", "tfw_pulse2.c")
    body = t[t.index("void tfw_pulse2_clear_stats(void)"):]
    body = body[:body.index("\n}")]
    assert "s_upd." not in body and "s_upd_clear_req = true" in body
    assert re.search(r"if \(s_upd_clear_req\) \{[^}]*s_upd\.max_us = 0", t)
