"""Python register constants == C header (TFW-05 / TFW-01 consistency)."""
import os
import re

from conftest import ROOT
from tfw_host import regs

HDR = os.path.join(ROOT, "components", "tfw_logic", "include", "tfw_regs.h")


def _parse():
    d = {}
    for m in re.finditer(r"#define\s+(TFW_\w+)\s+(0x[0-9A-Fa-f]+|\d+)\b", open(HDR).read()):
        d[m.group(1)] = int(m.group(2), 0)
    return d


def test_every_python_register_matches_header():
    h = _parse()
    for name, addr in regs.R.items():
        key = "TFW_R_" + name
        assert key in h, key
        assert h[key] == addr, (key, hex(h[key]), hex(addr))


def test_every_header_register_known_to_python():
    h = _parse()
    names = {k[6:] for k in h if k.startswith("TFW_R_")}
    assert names == set(regs.R), names ^ set(regs.R)


def test_commands_and_status_match_header():
    h = _parse()
    for k, v in regs.CMD.items():
        assert h["TFW_CMD_" + k] == v
    names = {"IDLE": "TFW_ST_IDLE", "RUNNING": "TFW_ST_RUNNING", "DONE": "TFW_ST_DONE", "REJECT_BUSY": "TFW_ST_REJECT_BUSY",
             "BAD_VALUE": "TFW_ST_BAD_VALUE", "NO_RESOURCE": "TFW_ST_NO_RESOURCE"}
    for k, c in names.items():
        assert h[c] == regs.STATUS[k]
    assert h["TFW_WIN_BASE"] == regs.WIN_BASE and h["TFW_WIN_END"] == regs.WIN_END
