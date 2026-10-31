"""sdkconfig combination syntax check and static lints of the product firmware (no ESP-IDF needed).

1. Every firmware C file is compiled with `gcc -fsyntax-only` against a stub of the ESP-IDF headers (idf_stub/), once per
   sdkconfig combination. The stub sdkconfig.h follows the ESP-IDF rule: an int option is always defined, a bool option is
   defined as 1 when it is y and NOT defined at all when it is n (review N-01 of the test firmware).
2. fw_cfg.h is checked with static asserts that its always-defined 0/1 macros follow the options.
3. Lints: CONFIG_ bool names only in #if/#ifdef lines, every CONFIG_FW_* used is declared in Kconfig, default pins match
   the signal table of design-baseline-v0.4 sec.2 / D-069 and are unique.
Scope: syntax and naming only. The ESP-IDF API signatures in idf_stub/ are from memory and not checked against 5.5.3."""
import itertools
import os
import re
import shutil
import subprocess

import pytest

from conftest import FW, HERE

STUB = os.path.join(HERE, "idf_stub")
COMP = os.path.join(FW, "components")
INCS = [os.path.join(COMP, "fw_core", "include"), os.path.join(COMP, "fw_hal", "include"),
        os.path.join(COMP, "fw_hal", "port_esp32p4"), os.path.join(COMP, "fw_motion", "include"),
        os.path.join(COMP, "fw_modbus", "include"), os.path.join(COMP, "fw_modbus"), os.path.join(FW, "main")]
SOURCES = []
for d in ("fw_core", "fw_hal", "fw_motion", "fw_modbus"):
    for root, _, files in os.walk(os.path.join(COMP, d)):
        SOURCES += [os.path.join(root, f) for f in sorted(files) if f.endswith(".c")]
SOURCES.append(os.path.join(FW, "main", "app_main.c"))
HOST_HEADERS = {"stdbool.h", "stddef.h", "stdint.h", "stdio.h", "stdlib.h", "string.h", "errno.h", "unistd.h", "math.h",
                "sys/socket.h", "sys/time.h", "sys/param.h", "netinet/in.h", "arpa/inet.h"}

pytestmark = pytest.mark.skipif(shutil.which("gcc") is None, reason="gcc not available")


def kconfig_items():
    txt = open(os.path.join(FW, "main", "Kconfig.projbuild"), encoding="utf-8").read()
    out = {}
    for blk in re.split(r"^\s*config ", txt, flags=re.M)[1:]:
        name = blk.split()[0]
        m = re.search(r"^\s+(bool|int)\b", blk, re.M)
        d = re.search(r"^\s+default (-?\w+)", blk, re.M)
        out[name] = (m.group(1), d.group(1) if d else ("n" if m.group(1) == "bool" else "0"))
    return out


def fragment(name):
    out = {}
    for line in open(os.path.join(FW, name), encoding="utf-8"):
        m = re.match(r"(CONFIG_\w+)=(.*)$", line.strip())
        if m:
            out[m.group(1)] = "1" if m.group(2) == "y" else m.group(2)
    return out


def make_sdkconfig(tmp, name, overrides=None, drop=()):
    defs = {"CONFIG_FREERTOS_HZ": "1000", "CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ": "400"}
    for n, (t, d) in kconfig_items().items():
        if t == "int":
            defs["CONFIG_" + n] = d
        elif d == "y":
            defs["CONFIG_" + n] = "1"                          # bool n: not defined
    defs.update({k: v for k, v in fragment("sdkconfig.defaults").items() if v != "n"})
    for k in drop:
        defs.pop(k, None)
    for k, v in (overrides or {}).items():
        if v is None:
            defs.pop(k, None)
        else:
            defs[k] = v
    d = os.path.join(str(tmp), name, "inc")
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "sdkconfig.h"), "w") as f:
        for k, v in sorted(defs.items()):
            f.write("#define %s %s\n" % (k, v))
    return d


def make_shims(tmp):
    d = os.path.join(str(tmp), "shims")
    os.makedirs(d, exist_ok=True)
    local = {os.path.basename(p) for inc in INCS for p in [os.path.join(inc, f) for f in os.listdir(inc)]}
    headers = [os.path.join(i, f) for i in INCS for f in os.listdir(i) if f.endswith(".h")]
    for src in SOURCES + headers:
        for m in re.finditer(r'#include\s+[<"]([^>"]+)[>"]', open(src, encoding="utf-8").read()):
            h = m.group(1)
            if h in HOST_HEADERS or h == "sdkconfig.h" or os.path.basename(h) in local:
                continue
            p = os.path.join(d, h)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            open(p, "w").close()
    return d


def gcc(src, sdk_inc, shims, extra=()):
    cmd = ["gcc", "-std=gnu11", "-fsyntax-only", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Werror=implicit-function-declaration",
           "-Werror=incompatible-pointer-types", "-fno-math-errno", "-include", os.path.join(STUB, "stub_idf.h"),
           "-I" + sdk_inc, "-I" + shims] + ["-I" + i for i in INCS] + list(extra) + [src]
    return subprocess.run(cmd, capture_output=True, text=True)


COMBOS = {
    "defaults": {},
    "one_axis": {"CONFIG_FW_AXIS_COUNT": "1"},
    "rtu_model": {"CONFIG_FW_MODEL_RTU": "1"},
    "one_axis_rtu": {"CONFIG_FW_AXIS_COUNT": "1", "CONFIG_FW_MODEL_RTU": "1"},
    "active_levels_flipped": {"CONFIG_FW_STOP_ACTIVE_LOW": "1", "CONFIG_FW_START_ACTIVE_LOW": None, "CONFIG_FW_OE_ACTIVE_LOW": "1"},
    "period_immediate": {"CONFIG_FW_PERIOD_IMMEDIATE": "1"},
    "iram_safe_off": {"CONFIG_PCNT_ISR_IRAM_SAFE": None, "CONFIG_MCPWM_ISR_IRAM_SAFE": None},
    "all_bools_on": {"CONFIG_FW_MODEL_RTU": "1", "CONFIG_FW_STOP_ACTIVE_LOW": "1", "CONFIG_FW_START_ACTIVE_LOW": "1",
                     "CONFIG_FW_OE_ACTIVE_LOW": "1", "CONFIG_FW_PERIOD_IMMEDIATE": "1"},
    "irq_levels_zero": {"CONFIG_FW_IRQ_PRIO_STOP": "0", "CONFIG_FW_IRQ_PRIO_PCNT": "0"},
}


@pytest.fixture(scope="module")
def shims(tmp_path_factory):
    return make_shims(tmp_path_factory.mktemp("shims"))


@pytest.mark.parametrize("combo", sorted(COMBOS))
def test_all_sources_compile(combo, shims, tmp_path):
    sdk = make_sdkconfig(tmp_path, combo, COMBOS[combo])
    for src in SOURCES:
        r = gcc(src, sdk, shims)
        assert r.returncode == 0, "%s [%s]:\n%s" % (os.path.relpath(src, FW), combo, r.stderr[-3000:])
        assert "warning" not in r.stderr, "%s [%s] warnings:\n%s" % (os.path.relpath(src, FW), combo, r.stderr[-3000:])


EXPECT_TMPL = """
#include "fw_cfg.h"
_Static_assert(FW_B_MODEL_RTU == %(rtu)d, "rtu");
_Static_assert(FW_B_STOP_ACTIVE_LOW == %(stop)d, "stop");
_Static_assert(FW_B_START_ACTIVE_LOW == %(start)d, "start");
_Static_assert(FW_B_OE_ACTIVE_LOW == %(oe)d, "oe");
_Static_assert(FW_B_PERIOD_IMMEDIATE == %(imm)d, "imm");
_Static_assert(FW_B_PCNT_ISR_IRAM_SAFE == %(iram)d, "iram");
_Static_assert(FW_AXIS_COUNT == %(axes)d, "axes");
int fw_cfg_check_dummy;
"""


@pytest.mark.parametrize("combo", sorted(COMBOS))
def test_cfg_macros_follow_options(combo, shims, tmp_path):
    sdk = make_sdkconfig(tmp_path, combo, COMBOS[combo])
    defs = dict(re.findall(r"#define (\w+) (\S+)", open(os.path.join(sdk, "sdkconfig.h")).read()))
    exp = {"rtu": int("CONFIG_FW_MODEL_RTU" in defs), "stop": int("CONFIG_FW_STOP_ACTIVE_LOW" in defs),
           "start": int("CONFIG_FW_START_ACTIVE_LOW" in defs), "oe": int("CONFIG_FW_OE_ACTIVE_LOW" in defs),
           "imm": int("CONFIG_FW_PERIOD_IMMEDIATE" in defs), "iram": int("CONFIG_PCNT_ISR_IRAM_SAFE" in defs),
           "axes": int(defs["CONFIG_FW_AXIS_COUNT"])}
    c = os.path.join(str(tmp_path), "cfg_check.c")
    open(c, "w").write(EXPECT_TMPL % exp)
    r = gcc(c, sdk, shims)
    assert r.returncode == 0, r.stderr


@pytest.mark.parametrize("n", ["0", "3"])
def test_axis_count_out_of_range_is_an_error(n, shims, tmp_path):
    sdk = make_sdkconfig(tmp_path, "bad" + n, {"CONFIG_FW_AXIS_COUNT": n})
    c = os.path.join(str(tmp_path), "cfg_check.c")
    open(c, "w").write('#include "fw_cfg.h"\n')
    r = gcc(c, sdk, shims)
    assert r.returncode != 0 and "must be 1 or 2" in r.stderr


def test_bool_options_only_in_preprocessor_lines():
    """N-01: CONFIG_<bool> may appear only in #if / #ifdef lines (or inside fw_cfg.h which guards with defined())."""
    bools = {"CONFIG_" + n for n, (t, _) in kconfig_items().items() if t == "bool"}
    bools |= {"CONFIG_PCNT_ISR_IRAM_SAFE", "CONFIG_MCPWM_ISR_IRAM_SAFE"}
    bad = []
    for src in SOURCES + [os.path.join(COMP, "fw_hal", "include", "fw_cfg.h")]:
        if src.endswith("fw_cfg.h"):
            continue
        for i, line in enumerate(open(src, encoding="utf-8"), 1):
            code = line.split("//")[0]
            if any(re.search(r"\b%s\b" % b, code) for b in bools) and not re.match(r"\s*#\s*(if|ifdef|ifndef|elif)\b", code):
                if code.strip().startswith(("/*", "*")):       # comment text
                    continue
                bad.append("%s:%d: %s" % (os.path.relpath(src, FW), i, line.strip()))
    assert not bad, "\n".join(bad)


def test_every_config_used_is_declared():
    declared = {"CONFIG_" + n for n in kconfig_items()}
    idf_known = {"CONFIG_PCNT_ISR_IRAM_SAFE", "CONFIG_MCPWM_ISR_IRAM_SAFE", "CONFIG_FREERTOS_HZ", "CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ"}
    used = set()
    for src in SOURCES + [os.path.join(COMP, "fw_hal", "include", "fw_cfg.h")]:
        used |= set(re.findall(r"\bCONFIG_[A-Z0-9_]+\b", open(src, encoding="utf-8").read()))
    unknown = sorted(u for u in used if u not in declared and u not in idf_known)
    assert not unknown, unknown
    unused = sorted(d for d in declared if d not in used)
    assert unused == [], "declared but never used: %s" % unused


# signal table: design-baseline-v0.4 sec.2 and D-069
EXPECTED_PINS = {
    "FW_X_PLS_GPIO": 25, "FW_X_DIR_GPIO": 32, "FW_Y_PLS_GPIO": 20, "FW_Y_DIR_GPIO": 21, "FW_OE_GPIO": 33,
    "FW_X_START_GPIO": 54, "FW_X_STOP_GPIO": 19, "FW_X_DI3_GPIO": 18, "FW_X_DI4_GPIO": 17,
    "FW_Y_START_GPIO": 7, "FW_Y_STOP_GPIO": 8, "FW_Y_DI7_GPIO": 15, "FW_Y_DI8_GPIO": 16,
    "FW_DO1_GPIO": 23, "FW_DO2_GPIO": 26, "FW_DO3_GPIO": 27, "FW_DO4_GPIO": 46,
    "FW_RS485_TX_GPIO": 24, "FW_RS485_RX_GPIO": 47, "FW_RS485_DE_GPIO": 48, "FW_PHY_RESET_GPIO": 51,
}


def test_default_pins_match_baseline_and_are_unique():
    items = kconfig_items()
    for k, v in EXPECTED_PINS.items():
        assert items[k] == ("int", str(v)), k
    gpios = [int(d) for n, (t, d) in items.items() if n.endswith("_GPIO")]
    assert len(gpios) == len(set(gpios)) == len(EXPECTED_PINS)


def test_product_defaults():
    items = kconfig_items()
    assert items["FW_AXIS_COUNT"] == ("int", "2")                # 1/2 axes, default 2
    assert items["FW_MODEL_RTU"] == ("bool", "n")
    assert items["FW_STOP_ACTIVE_LOW"] == ("bool", "n")          # NC contact: open = HIGH = STOP
    assert items["FW_START_ACTIVE_LOW"] == ("bool", "y")
    assert items["FW_OE_ACTIVE_LOW"] == ("bool", "n")            # GPIO33 HIGH = allowed
    assert items["FW_TCP_PORT"] == ("int", "502")
    assert items["FW_IRQ_PRIO_STOP"] == ("int", "3")


def test_sdkconfig_defaults_are_wellformed_and_safe():
    lines = [l.strip() for l in open(os.path.join(FW, "sdkconfig.defaults"), encoding="utf-8") if l.strip() and not l.startswith("#")]
    for l in lines:
        assert re.match(r'^CONFIG_[A-Z0-9_]+=(y|n|-?\d+|0x[0-9a-fA-F]+|".*")$', l), l
    frag = fragment("sdkconfig.defaults")
    assert frag["CONFIG_FREERTOS_HZ"] == "1000"
    for k in ("CONFIG_PCNT_ISR_IRAM_SAFE", "CONFIG_MCPWM_ISR_IRAM_SAFE", "CONFIG_GPIO_CTRL_FUNC_IN_IRAM",
              "CONFIG_ESP_TASK_WDT_PANIC", "CONFIG_ESP_INT_WDT"):
        assert frag.get(k) == "1", k
    assert frag["CONFIG_IDF_TARGET"] == '"esp32p4"'


def test_gpio33_is_forbidden_first_in_app_main():
    src = open(os.path.join(FW, "main", "app_main.c"), encoding="utf-8").read()
    body = src[src.index("void app_main"):]
    first_call = re.search(r"\n\s+(\w+)\(", body).group(1)
    assert first_call == "fw_hal_oe_early_forbid"


def test_negative_control_undefined_bool_in_expression_fails(shims, tmp_path):
    """The harness must reproduce the N-01 failure: a bool option that is 'n' is not defined, so using it as a value fails."""
    sdk = make_sdkconfig(tmp_path, "neg", {})
    c = os.path.join(str(tmp_path), "neg.c")
    open(c, "w").write('#include "sdkconfig.h"\nint x = CONFIG_FW_MODEL_RTU;\n')
    r = gcc(c, sdk, shims)
    assert r.returncode != 0
    sdk2 = make_sdkconfig(tmp_path, "neg2", {"CONFIG_FW_MODEL_RTU": "1"})
    assert gcc(c, sdk2, shims).returncode == 0


@pytest.mark.parametrize("model,axes,rtu", [("1axis_tcp", "1", False), ("1axis_rtu", "1", True), ("2axis_tcp", "2", False), ("2axis_rtu", "2", True)])
def test_model_fragments(model, axes, rtu, shims, tmp_path):
    """The four product models (1/2 axes x TCP/RTU) are sdkconfig fragments; each compiles in the combination it selects."""
    name = "sdkconfig.model_" + model
    lines = [l.strip() for l in open(os.path.join(FW, name), encoding="utf-8") if l.strip() and not l.startswith("#")]
    for l in lines:
        assert re.match(r"^CONFIG_[A-Z0-9_]+=(y|n|-?\d+)$", l), l
    frag = fragment(name)
    assert frag["CONFIG_FW_AXIS_COUNT"] == axes and (frag.get("CONFIG_FW_MODEL_RTU") == "1") == rtu
    sdk = make_sdkconfig(tmp_path, model, {k: v for k, v in frag.items()})
    for src in SOURCES:
        r = gcc(src, sdk, shims)
        assert r.returncode == 0 and "warning" not in r.stderr, "%s [%s]:\n%s" % (os.path.relpath(src, FW), model, r.stderr[-2000:])


def test_stop_isr_iram_flag_is_unconditional_and_mux_protected():
    """review m-05 / M-01: the STOP ISR service always asks for IRAM (not tied to the PCNT option); the latch is
    set (ISR, level guard) and released (task) under the same portMUX."""
    src = open(os.path.join(COMP, "fw_hal", "port_esp32p4", "hal_io.c"), encoding="utf-8").read()
    assert "flags |= ESP_INTR_FLAG_IRAM;" in src
    assert not re.search(r"#if[^\n]*PCNT_ISR_IRAM_SAFE[^\n]*\n\s*flags", src)
    assert src.count("portENTER_CRITICAL_ISR(&s_stop_mux)") == 1 and src.count("portENTER_CRITICAL(&s_stop_mux)") >= 4
    # D-059 / M-06: the product cannot use a fault-less axis
    ax = open(os.path.join(COMP, "fw_motion", "fw_axis.c"), encoding="utf-8").read()
    assert "FW_BOOT_FAULT_X" in ax and "fw_hal_stop_release" in ax
