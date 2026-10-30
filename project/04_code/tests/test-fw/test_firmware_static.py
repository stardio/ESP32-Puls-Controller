"""Source-level regression checks for the firmware parts that cannot run on the host (no ESP-IDF here).
They pin the review round 1 fixes (M-01, M-03, M-04, M-06, M-08, m-07, m-09) so that they cannot silently regress.
They prove the SOURCE shape only; behaviour on the board is still a HIL item."""
import os
import re

from conftest import ROOT


def src(*p):
    with open(os.path.join(ROOT, *p), encoding="utf-8") as f:
        return f.read()


def func_body(text, name):
    m = re.search(r"\b" + re.escape(name) + r"\s*\([^)]*\)\s*\{", text)
    assert m, name
    i, depth = m.end(), 1
    while depth:
        depth += {"{": 1, "}": -1}.get(text[i], 0)
        i += 1
    return text[m.end():i - 1]


def kconfig_default(name):
    t = src("main", "Kconfig.projbuild")
    m = re.search(r"config " + name + r"\n(?:.*\n)*?\s+default (-?\d+)", t)
    assert m, name
    return int(m.group(1))


# ---- M-01
def test_m01_es_trip_is_inline_not_a_flash_function():
    hdr = src("components", "tfw_logic", "include", "tfw_logic.h")
    core = src("components", "tfw_logic", "tfw_core.c")
    assert re.search(r"TFW_INLINE void tfw_es_trip\(", hdr)
    assert "TFW_INLINE" in hdr.split("tfw_es_trip")[0].splitlines()[-1]
    assert not re.search(r"^void tfw_es_trip", core, re.M)        # no out-of-line copy in flash
    assert "always_inline" in hdr


def test_m01_es_isr_calls_only_iram_safe_things():
    t = src("components", "tfw_estop_io", "tfw_estop_io.c")
    body = re.sub(r"/\*.*?\*/", "", func_body(t, "es_isr"), flags=re.S)
    calls = set(re.findall(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(", body)) - {"if", "void"}
    assert calls <= {"DBG_ESTOP", "tfw_pulse2_count_isr", "tfw_pulse2_stop", "tfw_es_trip", "esp_cpu_get_cycle_count"}, calls
    assert "esp_timer_get_time" not in body                       # was called from the ISR before
    assert "IRAM_ATTR" in t[:t.index("es_isr(void *arg)")].splitlines()[-1] or "IRAM_ATTR es_isr" in t
    assert "bool IRAM_ATTR tfw_estop_input_active" in t           # used inside the ISR-reachable debug/active path


def test_m01_iram_flag_only_in_iram_safe_build():
    t = src("components", "tfw_estop_io", "tfw_estop_io.c")
    m = re.search(r"#if CONFIG_PCNT_ISR_IRAM_SAFE\s*\n\s*flags \|= ESP_INTR_FLAG_IRAM;\s*\n#endif", t)
    assert m
    assert not re.search(r"gpio_install_isr_service\(ESP_INTR_FLAG_IRAM\)", t)


def test_m01_check_script_lists_the_isr_path():
    t = src("tools", "check_iram.sh")
    for f in ("tfw_es_trip", "tfw_upd_record", "es_isr", "pcnt_unit_get_count", "mcpwm_generator_set_force_level"):
        assert f in t, f


# ---- M-03
def test_m03_estop_has_highest_interrupt_level():
    e, p, g = (kconfig_default(n) for n in ("TFW_IRQ_PRIO_ESTOP", "TFW_IRQ_PRIO_PCNT", "TFW_IRQ_PRIO_GPT"))
    assert e > p > g > 0
    assert "intr_priority = CONFIG_TFW_IRQ_PRIO_PCNT" in src("components", "tfw_pulse2", "tfw_pulse2.c")
    assert "intr_priority = CONFIG_TFW_IRQ_PRIO_GPT" in src("components", "tfw_pulse2", "tfw_pulse2.c")
    assert "ESP_INTR_FLAG_LEVEL3" in src("components", "tfw_estop_io", "tfw_estop_io.c")


# ---- M-04
def test_m04_boot_state_is_latched_and_used_for_every_run_path():
    es = src("components", "tfw_estop_io", "tfw_estop_io.c")
    assert re.search(r"tfw_es_boot_check\(&s_es, tfw_estop_input_active\(\)\)", es)
    assert "return tfw_es_blocking(&s_es, tfw_estop_input_active())" in es
    main = src("main", "tfw_main.c")
    assert "tfw_cmd_check(&g_win, cmd, s_seq_running, tfw_estop_blocking()" in main
    assert "tfw_estop_latched()" not in main and "tfw_estop_latched()" not in src("components", "tfw_modbus", "tfw_modbus.c")
    assert "!tfw_estop_blocking()" in src("components", "tfw_modbus", "tfw_modbus.c")


# ---- M-06
def test_m06_late_reference_is_not_cfg_period_in_task_mode():
    t = src("components", "tfw_pulse2", "tfw_pulse2.c")
    assert "tfw_upd_record(&s_upd, c0, c1, s_cpu_mhz, s_ref_us)" in t
    assert "s_ref_us = tfw_upd_ref_us(mode == TFW_UPD_TASK_10MS, upd_us)" in t


# ---- M-08
def test_m08_axis2_and_probe_pins_default_to_unset():
    for n in ("TFW_AX2_PLS_GPIO", "TFW_AX2_DIR_GPIO", "TFW_AX2_EN_GPIO", "TFW_PROBE_GPIO"):
        assert kconfig_default(n) == -1, n
    t = src("components", "tfw_pulse2", "tfw_pulse2.c")
    assert "tfw_pin_conflict(ax2, 3, all1" in t and "probe_ok" in t


# ---- m-07 / m-09 / m-02 / m-01
def test_m07_no_math_errno_for_inline_sqrtf():
    assert "-fno-math-errno" in src("CMakeLists.txt")


def test_m09_clear_stats_does_not_write_the_nvs_counters():
    body = func_body(src("main", "tfw_main.c"), "clear_stats")
    assert "g_res.nvs_writes" not in body and "s_nvs_clear_req = true" in body


def test_m02_measured_cpu_clock_reaches_the_timing_code():
    main = src("main", "tfw_main.c")
    assert "tfw_pulse2_set_cpu_mhz(s_cpu_mhz)" in main and "tfw_estop_set_cpu_mhz(s_cpu_mhz)" in main


def test_m01_latency_product_is_saturating():
    assert "tfw_sat_mul(tfw_sat_mul(past, a->cur_ticks), s_ns_per_tick)" in src("components", "tfw_pulse2", "tfw_pulse2.c")
