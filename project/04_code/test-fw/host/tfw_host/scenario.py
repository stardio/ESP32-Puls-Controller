"""Scenario runner (TFW-05). A scenario file (YAML or JSON) lists steps; each step belongs to one T-ID.
HIL only: the firmware and the hardware are needed for real runs (tests use the local mock server)."""
import glob
import hashlib
import json
import math
import os
import time

from . import regs
from .expect import expected_edges
from .judge import judge
from .la import capture_span, count_edges, first_periods, freq_stats, latency
from .load import start_load
from .modbus import ModbusTcpClient, ModbusError, ModbusTimeout

R = regs.R


def _load_file(path):
    with open(path, encoding="utf-8") as f:
        txt = f.read()
    if path.lower().endswith((".yaml", ".yml")):
        import yaml
        return yaml.safe_load(txt)
    return json.loads(txt)


class Dev:
    """Register helper with the retry rule: control commands are retried up to 3 times [assumption]."""

    def __init__(self, host, port, unit=1):
        self.cli = ModbusTcpClient(host, port, unit).connect()

    def _retry(self, fn, *a):
        last = None
        for _ in range(3):
            try:
                return fn(*a)
            except (ModbusTimeout, OSError, ModbusError) as e:
                last = e
                time.sleep(0.05)
        raise last

    def w16(self, name, v):
        self._retry(self.cli.write_register, R[name], v)

    def w32(self, name, v):
        self._retry(self.cli.write_registers, R[name], regs.split_u32(v))

    def r16(self, name):
        return self._retry(self.cli.read_holding, R[name], 1)[0]

    def r32(self, name):
        lo, hi = self._retry(self.cli.read_holding, R[name], 2)
        return regs.join_u32(lo, hi)

    def close(self):
        self.cli.close()


def read_results(dev: Dev) -> dict:
    out = {"status": dev.r16("STATUS"), "seq_done": dev.r32("SEQ_DONE"),
           "err_ax1": dev.r32("ERR_AX1"), "err_ax2": dev.r32("ERR_AX2"),
           "overshoot_ax1": dev.r32("OVS_AX1"), "overshoot_ax2": dev.r32("OVS_AX2"),
           "upd_late": dev.r32("UPD_LATE"), "upd_max_us": dev.r32("UPD_MAX_US"),
           "isr_lat_ns_ax1": dev.r32("ISR_LAT_AX1"), "isr_lat_ns_ax2": dev.r32("ISR_LAT_AX2"),
           "nvs_writes": dev.r32("NVS_WRITES"), "nvs_max_ms": dev.r32("NVS_MAX_MS"),
           "chip_rev": dev.r16("CHIP_REV"), "cpu_mhz": dev.r16("CPU_MHZ"),
           "last_target_ax1": dev.r32("LAST_TGT_AX1"), "last_pcnt_ax1": dev.r32("LAST_CNT_AX1"),
           "last_target_ax2": dev.r32("LAST_TGT_AX2"), "last_pcnt_ax2": dev.r32("LAST_CNT_AX2"),
           "mb_req_count": dev.r32("MB_REQ_COUNT"), "mb_proc_max_us": dev.r32("MB_PROC_MAX_US"),
           "mb_hist": dev._retry(dev.cli.read_holding, R["MB_HIST"], 8),
           "es_state": dev.r16("ES_STATE"), "es_trip_count": dev.r16("ES_TRIP_COUNT"),
           "es_pulses_after_ax1": dev.r32("ES_PULSES_AX1"), "es_pulses_after_ax2": dev.r32("ES_PULSES_AX2"),
           "es_bind_ok": dev.r16("ES_BIND_OK"), "es_boot_level": dev.r16("ES_BOOT_LEVEL"), "es_last_err": dev.r16("ES_LAST_ERR"),
           "cfg_flags": dev.r16("CFG_FLAGS"), "mb_clients": dev.r16("MB_CLIENTS"), "mb_max_clients": dev.r16("MB_MAX_CLIENTS")}
    return out


def _wait_done(dev: Dev, timeout_s: float, sample=None):
    t0 = time.time()
    while time.time() - t0 < timeout_s:
        st = dev.r16("STATUS")
        if sample:
            sample()
        if st != regs.STATUS["RUNNING"]:
            return st
        time.sleep(0.05)
    return None


# Input registers written before every run and read back afterwards (C-01): name -> step key, default
PARAMS = (("AXIS_MASK", "axis_mask", 3), ("FREQ", "freq_hz", 100000), ("COUNT", "count", 1000),
          ("ACCEL_MS", "accel_ms", 100), ("DECEL_MS", "decel_ms", 100), ("REPEAT", "repeat", 1), ("SEED", "seed", 0),
          ("LOAD_NVS_MS", "nvs_period_ms", 0), ("LOAD_CPU_PCT", "cpu_pct", 0))


def _write_params(dev: Dev, step: dict) -> dict:
    """Writes all inputs, then reads STATUS (0x11 = a value was ignored) and every value back."""
    for name, key, dflt in PARAMS:
        v = step.get(key, dflt)
        (dev.w32 if name in regs.U32 else dev.w16)(name, v)
    dev.w16("CMD", regs.CMD["SET_LOAD"])
    out = {"param_status": dev.r16("STATUS"), "param_mismatch": []}
    for name, key, dflt in PARAMS:
        want = step.get(key, dflt)
        got = (dev.r32 if name in regs.U32 else dev.r16)(name)
        if got != want:
            out["param_mismatch"].append(f"{name}: wrote {want}, read {got}")
    return out


def probe_latched_run(dev: Dev, wait_s: float = 0.3):
    """M-09: while the e-stop is latched / active, a RUN command must be refused with 0x10 and nothing may start.
    Returns 0 (refused, state unchanged), 1 (something started) or None (e-stop not latched: not measurable)."""
    if dev.r16("ES_STATE") == 0:
        return None
    before = dev.r32("SEQ_DONE")
    dev.w16("CMD", regs.CMD["RUN_FIXED"])
    st = dev.r16("STATUS")
    time.sleep(wait_s)
    st2 = dev.r16("STATUS")
    after = dev.r32("SEQ_DONE")
    refused = (st == regs.STATUS["REJECT_BUSY"] and st2 != regs.STATUS["RUNNING"] and st2 != regs.STATUS["DONE"]
               and after == before)
    return 0 if refused else 1


def run_step(step: dict, target: str) -> dict:
    host, _, port = target.partition(":")
    dev = Dev(host, int(port or 502), step.get("unit", 1))
    load = None
    try:
        # a stale status (0x11 of the previous step) must not leak into this one; STOP_ALL also ends a leftover run
        dev.w16("CMD", regs.CMD["STOP_ALL"])
        dev.w16("CMD", regs.CMD["CLEAR_STATS"])
        cleared = read_results(dev)
        clear_ok = (cleared["status"] != regs.STATUS["REJECT_BUSY"] and cleared["seq_done"] == 0 and
                    not any(cleared[k] for k in ("err_ax1", "err_ax2", "overshoot_ax1", "overshoot_ax2")))
        pinfo = _write_params(dev, step)
        if "es_path" in step:
            dev.w16("ES_PATH_SEL", step["es_path"])
        ld = step.get("tcp_load")
        if ld:
            load = start_load(target, ld.get("clients", 4), ld.get("fc", "MIX"), ld.get("qty", 125), ld.get("interval_ms", 0))
        seen = [0]
        smin = [None]
        trip_before = dev.r16("ES_TRIP_COUNT")        # N-02: T-07 needs the trip count to rise during the run
        t_settle = [None]

        def sample(final=False):
            n = dev.r16("MB_CLIENTS")
            seen[0] = max(seen[0], n)
            # R-01: minimum over the run. Samples before the settle time are skipped (connections still being made);
            # the final sample (load still running) always counts.
            if final or (t_settle[0] is not None and time.time() >= t_settle[0]):
                smin[0] = n if smin[0] is None else min(smin[0], n)
        cmd_name = step.get("cmd", "RUN_SEQ")
        t_run = time.time()
        t_settle[0] = t_run + float((ld or {}).get("settle_s", 1.0))
        dev.w16("CMD", regs.CMD[cmd_name])
        st = _wait_done(dev, step.get("timeout_s", 600), sample if ld else None)
        t_end = time.time()
        run_s = t_end - t_run
        if ld:
            sample(final=True)             # load still running here
        data = read_results(dev)
        data.update(pinfo)
        data.update(wait_status=st, timed_out=st is None, clear_ok=clear_ok, run_s=run_s, run_start_ts=t_run, run_end_ts=t_end,
                    mb_clients_max=seen[0] if ld else None, mb_clients_min=smin[0] if ld else None, es_trip_count_before=trip_before,
                    cmd=cmd_name, repeat=step.get("repeat", 1), axis_mask=step.get("axis_mask", 3),
                    seed_changed=(dev.r32("SEED") != step.get("seed", 0)))
        # stop load before the statistics are finalised
        if load:
            data["tcp_load"] = load.stop().summary()
            load = None
            keep = {k: data[k] for k in ("mb_clients",)}
            data.update(read_results(dev))
            data.update(keep)
        if step.get("latch_probe"):
            data["relaunch_after_latch"] = probe_latched_run(dev)
        dev.w16("LOAD_NVS_MS", 0)
        dev.w16("LOAD_CPU_PCT", 0)
        dev.w16("CMD", regs.CMD["SET_LOAD"])
        return data
    finally:
        if load:
            load.stop()
        dev.close()


def analyse_la(step: dict, data: dict):
    la = step.get("la_csv")
    if not la:
        return
    base = step.get("_base_dir", ".")
    path = la if os.path.isabs(la) else os.path.join(base, la)
    chans = step.get("la_pulse_channels", ["ax1", "ax2"])
    fs = [freq_stats(path, c) for c in chans]
    data["la_f_min_hz"] = min(f.f_min for f in fs)
    data["la_f_avg_hz"] = [f.f_avg for f in fs]
    data["la_f_max_hz"] = max(f.f_max for f in fs)
    data["la_edges"] = [count_edges(path, c) for c in chans]
    # C-02: expected edges per axis. Either given per axis (list) / for all axes (int), or reproduced from seed/count.
    exp = step.get("la_expected_edges")
    if exp is None:
        exp = expected_edges(step)
    elif isinstance(exp, int):
        exp = [exp] * len(chans)
    data["la_expected_edges"] = [exp[i] if i < len(exp) else None for i in range(len(chans))]
    if all(e is not None for e in data["la_expected_edges"]):
        data["external_count_matches"] = all(g == e for g, e in zip(data["la_edges"], data["la_expected_edges"]))
    if step.get("la_trigger_channel"):
        edge = step.get("la_trigger_edge", "fall")                         # m-06
        if "estop_active_low" in step:
            data["la_trigger_edge_mismatch"] = (edge != ("fall" if step["estop_active_low"] else "rise"))
        lat = [latency(path, step["la_trigger_channel"], c, edge) for c in chans]
        data["la_latency_us_max"] = max(l.lat_max_us for l in lat)
        data["la_latency_us_avg"] = [l.lat_avg_us for l in lat]
        data["la_pulses_after"] = [l.pulses_after for l in lat]
    if "freq_hz" in step and step.get("check_not_above_request"):
        data["freq_checked"] = True
        fs_hz = step.get("la_sample_rate_hz")
        if fs_hz:
            # n-1: tolerance from the analyser resolution (one sample off in the shortest period), not a guessed percentage
            limit = 1.0 / (1.0 / step["freq_hz"] - 1.0 / fs_hz) if 1.0 / step["freq_hz"] > 1.0 / fs_hz else float("inf")
            data["freq_tol_basis"] = f"LA {fs_hz} S/s: 1 sample"
        else:
            limit = step["freq_hz"] * (1 + step.get("tol_pct", 0.5) / 100)
            data["freq_tol_basis"] = f"tol_pct {step.get('tol_pct', 0.5)} % [assumption]"
        data["requests_exceeded"] = [round(f.f_max) for f in fs if f.f_max > limit]
    fp = step.get("la_first_periods")
    if fp:
        n = int(fp.get("n", 3))
        res_hz = int(fp.get("res_hz", 10_000_000))
        f_hz = int(fp.get("freq_hz", step.get("freq_hz", 0)))
        exp_us = math.ceil(res_hz / f_hz) / res_hz * 1e6          # tick count rounded up: the output never exceeds the request
        tol_us = 2.0 * 1e6 / float(step.get("la_sample_rate_hz", 10_000_000))    # each edge +-1 sample
        ch = fp.get("channel", chans[0])
        per = first_periods(path, ch, n)
        data["la_first_periods_us"] = per
        data["first_periods_check"] = {"n": n, "n_found": len(per), "expected_us": exp_us, "tol_us": tol_us,
                                       "bad": [x for x in per if abs(x - exp_us) > tol_us]}


def _judge_input(rec: dict) -> dict:
    step, data = rec["step"], rec["data"]
    jd = dict(data, freq_hz=step.get("freq_hz", 0), nvs_period_ms=step.get("nvs_period_ms", 0),
              tcp_clients=(step.get("tcp_load") or {}).get("clients", 0), hmi=bool(step.get("hmi", False)))
    if "tol_pct" in step:
        jd["tol_pct"] = step["tol_pct"]
    return jd


def _other_runs_with_hash(results_dir: str, sha: str, self_path):
    out = []
    for p in glob.glob(os.path.join(results_dir, "T-*.json")):
        if self_path and os.path.abspath(p) == os.path.abspath(self_path):
            continue
        try:
            with open(p, encoding="utf-8") as f:
                r = json.load(f)
        except (OSError, ValueError):
            continue
        if r.get("data", {}).get("la_sha256") == sha:
            out.append(os.path.basename(p))
    return out


def capture_check(data: dict, csv_path: str, results_dir: str, self_path=None, slack_s: float = 0.5) -> dict:
    """N-04: is this CSV the capture of THIS run? (1) modified after the run ended, (2) at least run_s long (first to
    last row, minus slack_s [assumption]), (3) its sha256 is not recorded by another run. Returns the data fields."""
    reasons = []
    with open(csv_path, "rb") as f:
        sha = hashlib.sha256(f.read()).hexdigest()
    mtime = os.stat(csv_path).st_mtime
    end_ts = data.get("run_end_ts")
    if end_ts is None:
        reasons.append("실행 종료 시각 기록 없음")
    elif mtime <= end_ts:
        reasons.append(f"CSV 수정 시각이 실행 종료보다 {end_ts - mtime:.1f} s 이전(이번 실행 이전에 만들어진 파일)")
    span = capture_span(csv_path)
    need = max(0.0, float(data.get("run_s", 0.0)) - slack_s)
    if span < need:
        reasons.append(f"캡처 길이 {span:.2f} s < 실행 시간 {data.get('run_s', 0.0):.2f} s - 여유 {slack_s} s")
    dup = _other_runs_with_hash(results_dir, sha, self_path)
    if dup:
        reasons.append(f"같은 CSV(sha256 {sha[:12]})가 다른 실행에도 쓰임: {dup}")
    return {"la_sha256": sha, "la_csv_mtime": mtime, "la_span_s": span, "la_fresh": not reasons, "la_fresh_reasons": reasons}


def attach_capture(rec: dict, csv_path: str, results_dir: str, self_path=None) -> dict:
    """Adds the capture to a result record that was run without it and judges it (CLI `judge`, or the interactive hook)."""
    step = dict(rec["step"], la_csv=os.path.abspath(csv_path), _base_dir=".")
    data = rec["data"]
    for k in [k for k in data if k.startswith("la_") or k in ("external_count_matches", "freq_checked", "requests_exceeded",
                                                              "first_periods_check", "freq_tol_basis")]:
        del data[k]
    data.update(capture_check(data, csv_path, results_dir, self_path, float(step.get("la_span_slack_s", 0.5))))
    analyse_la(step, data)
    rec["step"]["la_csv_attached"] = os.path.abspath(csv_path)
    rec["verdict"] = judge(rec["id"], _judge_input(rec))
    return rec


def _write(rec: dict, results_dir: str, name: str) -> str:
    p = os.path.join(results_dir, name)
    with open(p, "w", encoding="utf-8") as f:
        json.dump(rec, f, ensure_ascii=False, indent=1)
    return p


def judge_file(result_json: str, csv_path: str) -> dict:
    """`judge <result.json> <csv>`: attach the capture to an earlier run and (re)judge it."""
    with open(result_json, encoding="utf-8") as f:
        rec = json.load(f)
    if not rec.get("data"):
        raise ValueError("result has no run data (the run itself failed)")
    attach_capture(rec, csv_path, os.path.dirname(os.path.abspath(result_json)), result_json)
    _write(rec, os.path.dirname(os.path.abspath(result_json)), os.path.basename(result_json))
    return rec


def run_scenario(path: str, target: str, results_dir: str = "results", capture=None) -> dict:
    """Run every step of the scenario file against target 'ip:port'. Writes results/<T-ID>_<time>_<n>.json and returns
    {T-ID: [step results]}.
    N-04: the run and the judgement of logic-analyser data are separate. A step with `la_csv` is left PENDING after the run
    (run_s and the end time are in the JSON); the capture is attached later with `judge <json> <csv>`. `capture`, if given,
    is called as capture(step, rec) after the run (the operator exports the capture there) and returns the CSV path; the
    same checks (modified after the run, long enough, hash unused) apply."""
    sc = _load_file(path)
    base = os.path.dirname(os.path.abspath(path))
    os.makedirs(results_dir, exist_ok=True)
    out = {}
    for step in sc.get("steps", []):
        tid = step["id"]
        step = dict(step, _base_dir=base)
        t0 = time.strftime("%Y%m%d_%H%M%S")
        rec = {"id": tid, "name": step.get("name", ""), "target": target, "time": t0, "step": {k: v for k, v in step.items() if not k.startswith("_")}}
        name = f"{tid}_{t0}_{len(out.get(tid, []))}.json"
        try:
            data = run_step(step, target)
            data["iram_safe"] = step.get("iram_safe")
            rec["data"] = data
            if step.get("la_csv"):
                csv_path = capture(step, rec) if capture else None
                if csv_path:
                    # the file name is known only now: the duplicate-hash check must not count the record itself
                    attach_capture(rec, csv_path, results_dir, os.path.join(results_dir, name))
                else:
                    rec["verdict"] = {"status": "PENDING", "reasons": [
                        f"로직 분석기 CSV 대기: 실행 종료 후 캡처를 내보내 `judge {name} <csv>`로 붙인다 (run_s={data['run_s']:.1f} s)"]}
            else:
                rec["verdict"] = judge(tid, _judge_input(rec))
        except (ModbusError, OSError, KeyError, ValueError) as e:
            rec["data"] = rec.get("data") or {}
            rec["verdict"] = {"status": "FAIL", "reasons": [f"실행 오류: {type(e).__name__}: {e}"]}
        _write(rec, results_dir, name)
        out.setdefault(tid, []).append(rec)
    return out
