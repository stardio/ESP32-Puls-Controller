"""TFW-05 criteria 3 (against a simulated device only) and 4: scenario execution, result JSON, 14-ID report."""
import glob
import json
import os

from tfw_host import regs
from tfw_host.judge import ALL_IDS, judge
from tfw_host.mock import MockTfwServer
from tfw_host.report import make_report
from tfw_host.scenario import run_scenario


def test_run_scenario_t03_to_t09_with_mock(tmp_path):
    srv = MockTfwServer(run_time_s=0.05, max_clients=8).start()   # control connection + 4 load clients > 4
    try:
        steps = [{"id": t, "name": f"step {t}", "cmd": "RUN_SEQ", "axis_mask": 3, "freq_hz": 200000, "count": 1000,
                  "repeat": 10, "seed": 5, "timeout_s": 10} for t in ("T-03", "T-04", "T-05", "T-06", "T-07", "T-08", "T-09")]
        steps[2]["tcp_load"] = {"clients": 4, "fc": "MIX", "qty": 125, "interval_ms": 1}
        sc = tmp_path / "sc.json"
        sc.write_text(json.dumps({"steps": steps}))
        res = run_scenario(str(sc), f"127.0.0.1:{srv.port}", str(tmp_path / "results"))
        assert set(res) == {"T-03", "T-04", "T-05", "T-06", "T-07", "T-08", "T-09"}
        files = glob.glob(str(tmp_path / "results" / "T-*.json"))
        assert len(files) == 7
        r04 = json.load(open([f for f in files if "T-04" in f][0]))
        assert r04["data"]["seq_done"] == 10 and r04["data"]["err_ax1"] == 0
        assert r04["verdict"]["status"] == "PASS"
        r05 = json.load(open([f for f in files if "T-05" in f][0]))
        assert r05["data"]["tcp_load"]["requests"] > 0 and r05["data"]["tcp_load"]["errors"] == 0
    finally:
        srv.stop()


def test_scenario_registers_written_as_configured(tmp_path):
    srv = MockTfwServer().start()
    try:
        sc = tmp_path / "sc.json"
        sc.write_text(json.dumps({"steps": [{"id": "T-04", "cmd": "RUN_MOVE", "axis_mask": 2, "freq_hz": 123456,
                                             "count": 77777, "accel_ms": 33, "decel_ms": 44, "seed": 9}]}))
        run_scenario(str(sc), f"127.0.0.1:{srv.port}", str(tmp_path / "r"))
        g = srv.regs
        assert g[regs.R["AXIS_MASK"]] == 2
        assert regs.join_u32(g[regs.R["FREQ"]], g[regs.R["FREQ"] + 1]) == 123456
        assert regs.join_u32(g[regs.R["COUNT"]], g[regs.R["COUNT"] + 1]) == 77777
        assert g[regs.R["ACCEL_MS"]] == 33 and g[regs.R["DECEL_MS"]] == 44
    finally:
        srv.stop()


def test_unreachable_target_is_recorded_not_raised(tmp_path):
    sc = tmp_path / "sc.json"
    sc.write_text(json.dumps({"steps": [{"id": "T-04", "cmd": "RUN_SEQ"}]}))
    res = run_scenario(str(sc), "127.0.0.1:1", str(tmp_path / "r"))
    assert res["T-04"][0]["verdict"]["status"] == "FAIL"


def test_report_lists_all_14_ids_with_substitutions(tmp_path):
    rd = tmp_path / "results"
    rd.mkdir()
    rec = {"id": "T-04", "name": "x", "target": "t", "time": "now", "step": {},
           "data": {"seq_done": 1000, "err_ax1": 0, "err_ax2": 0, "upd_late": 0, "chip_rev": 301},
           "verdict": {"status": "PASS", "reasons": ["ok"]}}
    (rd / "T-04_a.json").write_text(json.dumps(rec))
    (rd / "meta.json").write_text(json.dumps({"firmware_hash": "abc123", "sdkconfig_diff": "iram_on"}))
    out = tmp_path / "report_G3_pretest.md"
    make_report(str(rd), str(out))
    txt = out.read_text(encoding="utf-8")
    for tid in ALL_IDS:
        assert f"| {tid} |" in txt, tid
    assert len(ALL_IDS) == 14
    assert txt.count("기존 실적으로 대체(D-028)") >= 3          # T-01, T-02, T-14 (1축)
    assert "CURRENT_STATE.md" in txt
    assert "abc123" in txt and "iram_on" in txt and "301" in txt
    assert "합격" in txt and "미실시" in txt


def test_report_empty_results_still_14_rows(tmp_path):
    rd = tmp_path / "r"
    rd.mkdir()
    out = tmp_path / "rep.md"
    make_report(str(rd), str(out))
    txt = out.read_text(encoding="utf-8")
    assert sum(1 for t in ALL_IDS if f"| {t} |" in txt) == 14


def test_judge_rules():
    assert judge("T-04", {"err_ax1": 0, "err_ax2": 0, "external_count_matches": True})["status"] == "PASS"
    assert judge("T-04", {"err_ax1": 1})["status"] == "FAIL"
    assert judge("T-04", {"external_count_matches": False})["status"] == "FAIL"
    assert judge("T-05", {"upd_late": 1})["status"] == "FAIL"
    assert judge("T-06", {"iram_safe": True, "overshoot_ax1": 2})["status"] == "FAIL"
    assert judge("T-06", {"iram_safe": False, "overshoot_ax1": 2})["status"] == "RECORD"
    assert judge("T-03", {"freq_hz": 200000, "la_f_min_hz": 199500.0})["status"] == "PASS"
    assert judge("T-03", {"freq_hz": 200000, "la_f_min_hz": 150000.0})["status"] == "FAIL"
    assert judge("T-03", {"freq_hz": 1000000, "la_f_min_hz": 900000.0})["status"] == "RECORD"
    assert judge("T-07", {"relaunch_after_latch": 1})["status"] == "FAIL"
    assert judge("T-09", {"requests_exceeded": [300000]})["status"] == "FAIL"
    assert judge("T-10", {})["status"] == "NOT_RUN"
