"""Scenario runner (TFW-05). A scenario file (YAML or JSON) lists steps; each step belongs to one T-ID.
HIL only: the firmware and the hardware are needed for real runs (tests use the local mock server)."""
import json
import os
import time

from . import regs
from .judge import judge
from .la import count_edges, freq_stats, latency
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
           "es_bind_ok": dev.r16("ES_BIND_OK"), "es_boot_level": dev.r16("ES_BOOT_LEVEL")}
    return out


def _wait_done(dev: Dev, timeout_s: float):
    t0 = time.time()
    while time.time() - t0 < timeout_s:
        st = dev.r16("STATUS")
        if st != regs.STATUS["RUNNING"]:
            return st
        time.sleep(0.05)
    return None


def run_step(step: dict, target: str) -> dict:
    host, _, port = target.partition(":")
    dev = Dev(host, int(port or 502), step.get("unit", 1))
    load = None
    try:
        dev.w16("CMD", regs.CMD["CLEAR_STATS"])
        dev.w16("AXIS_MASK", step.get("axis_mask", 3))
        dev.w32("FREQ", step.get("freq_hz", 100000))
        dev.w32("COUNT", step.get("count", 1000))
        dev.w16("ACCEL_MS", step.get("accel_ms", 100))
        dev.w16("DECEL_MS", step.get("decel_ms", 100))
        dev.w32("REPEAT", step.get("repeat", 1))
        dev.w32("SEED", step.get("seed", 0))
        dev.w16("LOAD_NVS_MS", step.get("nvs_period_ms", 0))
        dev.w16("LOAD_CPU_PCT", step.get("cpu_pct", 0))
        dev.w16("CMD", regs.CMD["SET_LOAD"])
        if "es_path" in step:
            dev.w16("ES_PATH_SEL", step["es_path"])
        ld = step.get("tcp_load")
        if ld:
            load = start_load(target, ld.get("clients", 4), ld.get("fc", "MIX"), ld.get("qty", 125), ld.get("interval_ms", 0))
        dev.w16("CMD", regs.CMD[step.get("cmd", "RUN_SEQ")])
        st = _wait_done(dev, step.get("timeout_s", 600))
        data = read_results(dev)
        data["wait_status"] = st
        data["timed_out"] = st is None
        # stop load before the statistics are finalised
        if load:
            data["tcp_load"] = load.stop().summary()
            load = None
            data.update(read_results(dev))
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
    if "la_expected_edges" in step:
        data["external_count_matches"] = all(e == step["la_expected_edges"] for e in data["la_edges"])
    if step.get("la_trigger_channel"):
        lat = [latency(path, step["la_trigger_channel"], c) for c in chans]
        data["la_latency_us_max"] = max(l.lat_max_us for l in lat)
        data["la_latency_us_avg"] = [l.lat_avg_us for l in lat]
        data["la_pulses_after"] = [l.pulses_after for l in lat]
    if "freq_hz" in step and step.get("check_not_above_request"):
        data["freq_checked"] = True
        data["requests_exceeded"] = [round(f.f_max) for f in fs if f.f_max > step["freq_hz"] * (1 + step.get("tol_pct", 0.5) / 100)]


def run_scenario(path: str, target: str, results_dir: str = "results") -> dict:
    """Run every step of the scenario file against target 'ip:port'. Writes results/<T-ID>_<time>.json and returns
    {T-ID: [step results]}."""
    sc = _load_file(path)
    base = os.path.dirname(os.path.abspath(path))
    os.makedirs(results_dir, exist_ok=True)
    out = {}
    for step in sc.get("steps", []):
        tid = step["id"]
        step = dict(step, _base_dir=base)
        t0 = time.strftime("%Y%m%d_%H%M%S")
        rec = {"id": tid, "name": step.get("name", ""), "target": target, "time": t0, "step": {k: v for k, v in step.items() if not k.startswith("_")}}
        try:
            data = run_step(step, target)
            analyse_la(step, data)
            data["iram_safe"] = step.get("iram_safe")
            rec["data"] = data
            rec["verdict"] = judge(tid, dict(data, freq_hz=step.get("freq_hz", 0), tol_pct=step.get("tol_pct", 0.5)))
        except (ModbusError, OSError, KeyError) as e:
            rec["data"] = {}
            rec["verdict"] = {"status": "FAIL", "reasons": [f"실행 오류: {type(e).__name__}: {e}"]}
        with open(os.path.join(results_dir, f"{tid}_{t0}_{len(out.get(tid, []))}.json"), "w", encoding="utf-8") as f:
            json.dump(rec, f, ensure_ascii=False, indent=1)
        out.setdefault(tid, []).append(rec)
    return out
