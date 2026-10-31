"""T-15 (D-070): CPU stop injection during output, then read reset evidence after the watchdog reboot.
The pulse duration and count after injection are measured on the logic analyser; this tool only drives the
device and records reset_reason/boot_count. Verdict is always RECORD (no PASS threshold is defined)."""
import json
import os
import time

from .modbus import ModbusTcpClient
from .regs import CMD, R, T15_MAGIC, split_u32


def run_t15(target: str, results: str, mode: str = "task", freq: int = 200000, count: int = 40000000,
            axis_mask: int = 3, reboot_timeout_s: float = 30.0) -> dict:
    host, _, port = target.partition(":")
    c = ModbusTcpClient(host, int(port or 502)).connect()
    before = c.read_holding(R["BOOT_COUNT"], 1)[0]
    c.write_register(R["AXIS_MASK"], axis_mask)
    c.write_registers(R["FREQ"], split_u32(freq))
    c.write_registers(R["COUNT"], split_u32(count))
    c.write_register(R["CMD"], CMD["RUN_FIXED"])
    time.sleep(1.0)
    status = c.read_holding(R["STATUS"], 1)[0]
    t_inject = time.time()
    c.write_register(R["T15_HANG"], T15_MAGIC[mode])
    c.close()
    rec = {"id": "T-15", "mode": mode, "freq_hz": freq, "count": count, "axis_mask": axis_mask,
           "status_before_inject": status, "boot_count_before": before, "inject_epoch": t_inject}
    deadline = time.time() + reboot_timeout_s
    while time.time() < deadline:
        time.sleep(1.0)
        try:
            c = ModbusTcpClient(host, int(port or 502), timeout=1.0).connect()
            rec["reset_reason"] = c.read_holding(R["RESET_REASON"], 1)[0]
            rec["boot_count_after"] = c.read_holding(R["BOOT_COUNT"], 1)[0]
            rec["reconnect_s"] = round(time.time() - t_inject, 1)
            c.close()
            break
        except OSError:
            continue
    rebooted = rec.get("boot_count_after") is not None and rec["boot_count_after"] != before
    if rec.get("boot_count_after") is None:
        why = "재접속 실패: 장치가 재부팅되지 않았거나 네트워크 미복구(워치독 미동작 의심)"
    elif not rebooted:
        why = "boot_count 불변: 주입이 무시됨(TFW_T15_FAULT_INJECT=n?)"
    else:
        why = f"워치독 재부팅 확인(reset_reason={rec['reset_reason']}). 주입 후 출력 시간·펄스 수·GPIO33은 로직 분석기 값으로 기록"
    rec["verdict"] = {"verdict": "RECORD", "reasons": [why]}
    os.makedirs(results, exist_ok=True)
    path = os.path.join(results, f"T-15_{mode}_{time.strftime('%Y%m%d-%H%M%S')}.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(rec, f, ensure_ascii=False, indent=1)
    rec["_path"] = path
    return rec
