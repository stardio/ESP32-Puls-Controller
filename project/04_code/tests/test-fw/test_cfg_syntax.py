"""N-01 (review round 2): compile tfw_main.c and tfw_estop_io.c with `gcc -fsyntax-only` against a stub of the ESP-IDF
headers, once per sdkconfig combination. The stub sdkconfig.h follows the ESP-IDF rule: an int option is always
defined, a bool option is defined as 1 when it is y and NOT defined at all when it is n.
Scope: proves only that the files compile for each option combination. The ESP-IDF API signatures in idf_stub/ are
from memory and are not checked against the real headers; real builds and HIL remain open."""
import os
import re
import shutil
import subprocess

import pytest

from conftest import ROOT

HERE = os.path.dirname(os.path.abspath(__file__))
STUB = os.path.join(HERE, "idf_stub")
FILES = [("main", "tfw_main.c"), ("components", "tfw_estop_io", "tfw_estop_io.c")]
INCLUDES = [("components", "tfw_logic", "include"), ("components", "tfw_estop_io", "include"),
            ("components", "tfw_pulse2", "include"), ("components", "tfw_modbus", "include"), ("main",)]

pytestmark = pytest.mark.skipif(shutil.which("gcc") is None, reason="gcc not available")


def kconfig_items():
    """[(name, type, default)] in file order, from main/Kconfig.projbuild."""
    txt = open(os.path.join(ROOT, "main", "Kconfig.projbuild"), encoding="utf-8").read()
    out = []
    for m in re.finditer(r"^\s*config (\w+)\n\s+(bool|int)\b[^\n]*\n((?:\s+(?:range|default|help|depends)[^\n]*\n|\s{8,}[^\n]*\n)*)", txt, re.M):
        d = re.search(r"default (-?\w+)", m.group(3))
        out.append((m.group(1), m.group(2), d.group(1) if d else ("n" if m.group(2) == "bool" else "0")))
    return out


def fragment(name):
    """sdkconfig fragment -> {CONFIG_X: '1'|value}; '# CONFIG_X is not set' and absent lines stay undefined."""
    p = os.path.join(ROOT, name)
    out = {}
    for line in open(p, encoding="utf-8"):
        m = re.match(r"(CONFIG_\w+)=(.*)$", line.strip())
        if m:
            out[m.group(1)] = "1" if m.group(2) == "y" else m.group(2)
    return out


def make_sdkconfig(tmp, overrides=None, fragments=()):
    defs = {"CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ": "400", "CONFIG_FREERTOS_HZ": "1000"}
    for n, t, d in kconfig_items():
        if t == "int":
            defs["CONFIG_" + n] = d
        elif d == "y":
            defs["CONFIG_" + n] = "1"                       # bool n: not defined
    for fr in fragments:
        defs.update(fragment(fr))
    for k, v in (overrides or {}).items():
        if v is None:
            defs.pop(k, None)
        else:
            defs[k] = v
    d = os.path.join(str(tmp), "inc")
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "sdkconfig.h"), "w") as f:
        for k, v in sorted(defs.items()):
            f.write(f"#define {k} {v}\n")
    return d, defs


COMBOS = {
    "default": ((), {}),
    "iram_on": (("sdkconfig.tfw_iram_on",), {}),
    "iram_off": (("sdkconfig.tfw_iram_off",), {}),
    "task_mode": ((), {"CONFIG_TFW_UPD_MODE_HWTIMER": None}),
    "active_high": ((), {"CONFIG_TFW_ESTOP_ACTIVE_LOW": None}),            # baseline v0.4 default (NC contact)
    "active_low": ((), {"CONFIG_TFW_ESTOP_ACTIVE_LOW": "1"}),
    "ax2_io": ((), {"CONFIG_TFW_AX2_IO_PRESENT": "1"}),
    "oe_active_low": ((), {"CONFIG_TFW_PULSE_OE_ACTIVE_LOW": "1", "CONFIG_TFW_START_ACTIVE_LOW": None}),
    "legacy_nvs": ((), {"CONFIG_TFW_LEGACY_NVS_ON_WRITE": "1"}),
    "iram_off_task_active_high": (("sdkconfig.tfw_iram_off",), {"CONFIG_TFW_UPD_MODE_HWTIMER": None, "CONFIG_TFW_ESTOP_ACTIVE_LOW": None}),
}


def compile_syntax(tmp, combo, parts):
    frs, ov = COMBOS[combo]
    inc, defs = make_sdkconfig(tmp, ov, frs)
    cmd = ["gcc", "-std=gnu11", "-fsyntax-only", "-Wall", "-Wno-unused-function", "-Werror=implicit-function-declaration",
           f"-I{inc}", f"-I{STUB}"] + [f"-I{os.path.join(ROOT, *i)}" for i in INCLUDES] + [os.path.join(ROOT, *parts)]
    return subprocess.run(cmd, capture_output=True, text=True), defs


def test_stub_follows_the_bool_rule(tmp_path):
    _, d = make_sdkconfig(tmp_path, {}, fragments=("sdkconfig.tfw_iram_off",))
    assert "CONFIG_PCNT_ISR_IRAM_SAFE" not in d and d["CONFIG_MCPWM_ISR_HANDLER_IN_IRAM"] == "1"
    _, d = make_sdkconfig(tmp_path)
    assert "CONFIG_TFW_LEGACY_NVS_ON_WRITE" not in d and d["CONFIG_TFW_UPD_MODE_HWTIMER"] == "1"
    assert d["CONFIG_TFW_AX2_PLS_GPIO"] == "20" and d["CONFIG_TFW_AX2_DIR_GPIO"] == "21"          # F-01, F-02
    assert d["CONFIG_TFW_PULSE_OE_GPIO"] == "33" and "CONFIG_TFW_PULSE_OE_ACTIVE_LOW" not in d    # F-03..F-05
    assert d["CONFIG_TFW_ESTOP_GPIO"] == "19" and d["CONFIG_TFW_ESTOP2_GPIO"] == "17" and "CONFIG_TFW_ESTOP_ACTIVE_LOW" not in d   # F-06, F-07
    assert d["CONFIG_TFW_START_GPIO"] == "54" and d["CONFIG_TFW_START2_GPIO"] == "18"
    assert "CONFIG_TFW_AX2_IO_PRESENT" not in d                                                   # D-068 default n
    bools = {n for n, t, _ in kconfig_items() if t == "bool"}
    assert {"TFW_ESTOP_ACTIVE_LOW", "TFW_UPD_MODE_HWTIMER", "TFW_LEGACY_NVS_ON_WRITE"} <= bools


@pytest.mark.parametrize("combo", list(COMBOS))
@pytest.mark.parametrize("parts", FILES, ids=lambda p: p[-1])
def test_n01_compiles_in_every_bool_option_combination(tmp_path, combo, parts):
    r, _ = compile_syntax(tmp_path, combo, parts)
    errs = [l for l in r.stderr.splitlines() if "error" in l]
    assert r.returncode == 0 and not errs, "\n".join(errs[:10]) or r.stderr[:1000]


def test_n01_stub_check_detects_the_round2_defect(tmp_path):
    """Self-test of the check: a bool option used in a C expression must NOT compile when the option is n."""
    src = tmp_path / "bad.c"
    src.write_text('#include "sdkconfig.h"\nint f(void) { return CONFIG_TFW_UPD_MODE_HWTIMER ? 1 : 0; }\n')
    inc, _ = make_sdkconfig(tmp_path, COMBOS["task_mode"][1])
    r = subprocess.run(["gcc", "-fsyntax-only", f"-I{inc}", str(src)], capture_output=True, text=True)
    assert r.returncode != 0 and "CONFIG_TFW_UPD_MODE_HWTIMER" in r.stderr


def test_n01_helper_macros_follow_the_rule(tmp_path):
    src = tmp_path / "h.c"
    src.write_text('#include "tfw_cfg.h"\n#if TFW_B_UPD_MODE_HWTIMER != 0 || TFW_B_LEGACY_NVS_ON_WRITE != 1 || TFW_B_ESTOP_ACTIVE_LOW != 0 || TFW_B_START_ACTIVE_LOW != 1 || TFW_B_PULSE_OE_ACTIVE_LOW != 0 || TFW_B_AX2_IO_PRESENT != 0 || TFW_B_PCNT_ISR_IRAM_SAFE != 0\n#error wrong\n#endif\nint x;\n')
    inc, _ = make_sdkconfig(tmp_path, {"CONFIG_TFW_UPD_MODE_HWTIMER": None, "CONFIG_TFW_LEGACY_NVS_ON_WRITE": "1"})
    r = subprocess.run(["gcc", "-fsyntax-only", f"-I{inc}", f"-I{os.path.join(ROOT, 'components', 'tfw_logic', 'include')}", str(src)],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stderr


def build_and_run_estop_host(tmp_path, combo="default", src_dir=None):
    """Compile the REAL tfw_estop_io.c + tfw_core.c with the IDF stubs and the controllable test stubs, then run it."""
    frs, ov = COMBOS[combo]
    inc, _ = make_sdkconfig(tmp_path, ov, frs)
    exe = os.path.join(str(tmp_path), "estop_host")
    cmd = ["gcc", "-std=gnu11", "-Wall", "-Wextra", "-O1", "-Wno-unused-parameter", f"-I{inc}", f"-I{STUB}"] + \
          [f"-I{os.path.join(ROOT, *i)}" for i in INCLUDES] + \
          [os.path.join(HERE, "estop_host", "test_estop_host.c"), os.path.join(ROOT, "components", "tfw_estop_io", "tfw_estop_io.c"),
           os.path.join(ROOT, "components", "tfw_logic", "tfw_core.c"), "-o", exe, "-lm"]
    b = subprocess.run(cmd, capture_output=True, text=True)
    assert b.returncode == 0, b.stderr[:2000]
    return subprocess.run([exe], capture_output=True, text=True)


@pytest.mark.parametrize("combo", ["default", "iram_on", "iram_off", "active_high", "active_low", "ax2_io"])
def test_n02_n03_estop_io_behaviour_on_host(tmp_path, combo):
    r = build_and_run_estop_host(tmp_path, combo)
    assert r.returncode == 0, r.stdout + r.stderr
    assert "failed=0" in r.stdout
