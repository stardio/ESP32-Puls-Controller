/* TFW-01 tfw_main. Based on servo1axis main/app_main.c [E4] (init_nvs, boot log style). The ladder runtime is NOT
 * included. NOT verified on hardware (no ESP-IDF / board in the authoring container). */
#include "tfw_main.h"
#include "tfw_cfg.h"
#include <string.h>
#include "sdkconfig.h"
#include "esp_chip_info.h"
#include "esp_clk_tree.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "tfw_estop_io.h"
#include "tfw_modbus.h"
#include "tfw_pulse2.h"

static const char *TAG = "TFW_MAIN";

#define BUILD_IRAM TFW_B_PCNT_ISR_IRAM_SAFE

static tfw_regwin_t g_win;
static volatile tfw_results_t g_res;
static TaskHandle_t s_seq_task, s_nvs_task;
static SemaphoreHandle_t s_cmd_lock;            /* m-03: serialises command handling of all Modbus client tasks */
static volatile bool s_nvs_clear_req;           /* m-09: the NVS task clears its own counters */
static volatile bool s_seq_running, s_abort;
static volatile uint16_t s_seq_cmd;
static uint32_t s_cpu_mhz;
static int s_core_comm, s_core_upd;

/* ------------------------------------------------------------------ NVS (init copied from [E4]) */
static void init_nvs(void)
{
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        error = nvs_flash_init();
    }
    ESP_ERROR_CHECK(error);
}

typedef struct { uint32_t magic, res_hz, upd_us, flags; } cfg_blob_t;
#define CFG_MAGIC 0x54465701u

static void cfg_load(void)
{
    nvs_handle_t h;
    cfg_blob_t b;
    size_t n = sizeof(b);
    if (nvs_open("tfw_cfg", NVS_READONLY, &h) != ESP_OK) return;
    if (nvs_get_blob(h, "cfg", &b, &n) == ESP_OK && n == sizeof(b) && b.magic == CFG_MAGIC &&
        tfw_res_valid(b.res_hz) && b.upd_us >= 500 && b.upd_us <= 10000 && (b.flags & ~0xFu) == 0) {
        g_win.cfg_res_hz = b.res_hz; g_win.cfg_upd_us = (uint16_t)b.upd_us;
        g_win.cfg_flags = (uint16_t)((g_win.cfg_flags & TFW_CFG_PCNT_IRAM) | (b.flags & TFW_CFG_WRITABLE_MASK));
    }
    nvs_close(h);
}

/* Persisted only when one of the three cfg registers is written (rare, idle only) - not on every Modbus write. */
static void cfg_save(void)
{
    cfg_blob_t b = {CFG_MAGIC, g_win.cfg_res_hz, g_win.cfg_upd_us, g_win.cfg_flags & TFW_CFG_WRITABLE_MASK};
    nvs_handle_t h;
    if (nvs_open("tfw_cfg", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "cfg", &b, sizeof(b));
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "cfg saved: res=%lu upd=%u flags=0x%x (applied at next boot)", (unsigned long)b.res_hz,
                 (unsigned)b.upd_us, (unsigned)b.flags);
    }
}

/* ------------------------------------------------------------------ results */
void tfw_get_results(tfw_results_t *out)
{
    memcpy(out, (const void *)&g_res, sizeof(*out));      /* single-writer fields, snapshot may be slightly torn between fields */
    tfw_pulse_stats_t ps[2];
    tfw_pulse2_stats(0, &ps[0]);
    tfw_pulse2_stats(1, &ps[1]);
    out->upd_late = ps[0].upd_late;
    out->upd_max_us = ps[0].upd_max_us;
    out->isr_lat_ns[0] = ps[0].isr_lat_max_ns;
    out->isr_lat_ns[1] = ps[1].isr_lat_max_ns;
    tfw_es_stats_t es;
    tfw_estop_stats(&es);
    out->es_state = es.state; out->es_trip_count = es.trip_count; out->es_isr_lat_ns = es.isr_lat_ns;
    out->es_pulses_after[0] = es.pulses_after[0]; out->es_pulses_after[1] = es.pulses_after[1];
    out->mb_max_clients = CONFIG_TFW_MAX_CLIENTS;
    out->es_bind_ok = es.bind_ok; out->es_boot_level = es.boot_level; out->es_last_err = es.last_err;
}

static void clear_stats(void)
{
    g_res.seq_done = 0;
    for (int i = 0; i < 2; ++i) { g_res.err[i] = 0; g_res.overshoot[i] = 0; g_res.last_target[i] = 0; g_res.last_pcnt[i] = 0; }
    s_nvs_clear_req = true;                          /* m-09: nvs_writes/nvs_max_ms are cleared by their single writer */
    if (s_nvs_task) xTaskNotifyGive(s_nvs_task);
    tfw_pulse2_clear_stats();
    tfw_modbus_clear_stats();
}

/* ------------------------------------------------------------------ sequencer glue */
static int hw_move(void *c, uint8_t ax, uint32_t sp, uint32_t cnt, uint32_t a, uint32_t d)
{ (void)c; if (tfw_estop_blocking()) return -1; tfw_pulse2_set_dir(ax, true); return tfw_pulse2_move(ax, sp, cnt, a, d) == ESP_OK ? 0 : -1; }
static int hw_fixed(void *c, uint8_t ax, uint32_t f, uint32_t cnt)
{ (void)c; if (tfw_estop_blocking()) return -1; tfw_pulse2_set_dir(ax, true); return tfw_pulse2_run_fixed(ax, f, cnt) == ESP_OK ? 0 : -1; }
static bool hw_busy(void *c, uint8_t ax) { (void)c; return tfw_pulse2_busy(ax); }
static uint32_t hw_count(void *c, uint8_t ax) { (void)c; return tfw_pulse2_count(ax); }
static void hw_stop(void *c, uint8_t ax) { (void)c; tfw_pulse2_stop(ax); }
static void hw_delay(void *c, uint32_t ms) { (void)c; vTaskDelay(pdMS_TO_TICKS(ms) ? pdMS_TO_TICKS(ms) : 1); }
static uint32_t hw_now(void *c) { (void)c; return (uint32_t)(esp_timer_get_time() / 1000); }
static bool hw_aborted(void *c) { (void)c; return s_abort || tfw_estop_blocking(); }

static void seq_task(void *arg)
{
    (void)arg;
    const tfw_seq_hw_t hw = {NULL, hw_move, hw_fixed, hw_busy, hw_count, hw_stop, hw_delay, hw_now, hw_aborted};
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        s_abort = false;
        const uint16_t cmd = s_seq_cmd;
        uint32_t n = tfw_seq_run(&hw, &g_win, &g_res, cmd);
        ESP_LOGI(TAG, "cmd %u finished: iterations=%lu seq_done=%lu err=%lu/%lu overshoot=%lu/%lu status=0x%x", (unsigned)cmd,
                 (unsigned long)n, (unsigned long)g_res.seq_done, (unsigned long)g_res.err[0], (unsigned long)g_res.err[1],
                 (unsigned long)g_res.overshoot[0], (unsigned long)g_res.overshoot[1], (unsigned)g_win.status);
        s_seq_running = false;
    }
}

static uint16_t handle_cmd_locked(uint16_t cmd)
{
    /* M-04: refuse while latched OR while the input is still active (also when it was active at boot) */
    uint16_t st = tfw_cmd_check(&g_win, cmd, s_seq_running, tfw_estop_blocking(), tfw_pulse2_axis_ok(1));
    if (st) { g_win.status = st; return st; }
    switch (cmd) {
    case TFW_CMD_RUN_FIXED: case TFW_CMD_RUN_MOVE: case TFW_CMD_RUN_SEQ:
        s_seq_cmd = cmd; s_seq_running = true; g_win.status = TFW_ST_RUNNING;
        xTaskNotifyGive(s_seq_task);
        break;
    case TFW_CMD_STOP_ALL:
        s_abort = true;
        tfw_pulse2_stop(0); tfw_pulse2_stop(1);
        if (!s_seq_running) g_win.status = TFW_ST_IDLE;
        break;
    case TFW_CMD_CLEAR_STATS: clear_stats(); break;
    case TFW_CMD_SET_LOAD: ESP_LOGI(TAG, "load: nvs_period=%u ms cpu=%u %%", g_win.load_nvs_ms, g_win.load_cpu_pct); break;
    }
    return 0;
}

uint16_t tfw_handle_cmd(uint16_t cmd)
{
    if (s_cmd_lock) xSemaphoreTake(s_cmd_lock, portMAX_DELAY);
    uint16_t r = handle_cmd_locked(cmd);
    if (s_cmd_lock) xSemaphoreGive(s_cmd_lock);
    return r;
}

static void on_effect(void *user, const tfw_wr_effect_t *fx)
{
    (void)user;
    if (s_cmd_lock) xSemaphoreTake(s_cmd_lock, portMAX_DELAY);     /* m-03 */
    if (fx->es_path_set) tfw_estop_set_path((uint8_t)g_win.es_path_sel);
    if (fx->es_release) { uint16_t r = tfw_estop_release(); if (r) g_win.status = r; }
    if (fx->cfg_changed) cfg_save();
    if (fx->cmd) handle_cmd_locked(fx->cmd);
    if (s_cmd_lock) xSemaphoreGive(s_cmd_lock);
}

static bool hook_seq_busy(void) { return s_seq_running; }
static void hook_results(void *user, tfw_results_t *out) { (void)user; tfw_get_results(out); }

/* ------------------------------------------------------------------ load tasks (lowest priority) */
static void nvs_load_task(void *arg)
{
    (void)arg;
    nvs_handle_t h;
    bool open = (nvs_open("tfw_load", NVS_READWRITE, &h) == ESP_OK);
    uint8_t blob[32] = {0};
    uint32_t seq = 0;
    for (;;) {
        if (s_nvs_clear_req) { s_nvs_clear_req = false; g_res.nvs_writes = 0; g_res.nvs_max_ms = 0; }   /* m-09 */
        uint32_t period = g_win.load_nvs_ms;
        if (!open || period == 0) { ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100)); continue; }
        memcpy(blob, &seq, sizeof(seq)); ++seq;
        int64_t t0 = esp_timer_get_time();
        esp_err_t e = nvs_set_blob(h, "blob", blob, sizeof(blob));
        if (e == ESP_OK) e = nvs_commit(h);
        uint32_t ms = (uint32_t)((esp_timer_get_time() - t0 + 999) / 1000);
        if (e == ESP_OK) {
            g_res.nvs_writes = g_res.nvs_writes + 1u;
            if (ms > g_res.nvs_max_ms) g_res.nvs_max_ms = ms;
        } else {
            ESP_LOGW(TAG, "nvs write failed 0x%x", (unsigned)e);
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(period));      /* woken early by CLEAR_STATS */
    }
}

static void cpu_load_task(void *arg)      /* [assumed] busy loop duty over a 100 ms window; preempted by higher tasks */
{
    (void)arg;
    for (;;) {
        uint32_t busy = tfw_load_busy_ms(g_win.load_cpu_pct, 100);
        if (busy == 0) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        int64_t end = esp_timer_get_time() + (int64_t)busy * 1000;
        while (esp_timer_get_time() < end) { __asm__ volatile("nop"); }
        vTaskDelay(pdMS_TO_TICKS(100 - busy));
    }
}

/* ------------------------------------------------------------------ boot */
typedef struct { SemaphoreHandle_t done; esp_err_t err; } init_arg_t;
static void pulse_init_task(void *p)         /* runs on the update core so the GPTimer interrupt is allocated there */
{
    init_arg_t *a = p;
    a->err = tfw_pulse2_init(CONFIG_TFW_AXIS_COUNT, g_win.cfg_res_hz, g_win.cfg_upd_us,
#if CONFIG_TFW_UPD_MODE_HWTIMER
                             TFW_UPD_HWTIMER);
#else
                             TFW_UPD_TASK_10MS);
#endif
    xSemaphoreGive(a->done);
    vTaskDelete(NULL);
}

void app_main(void)
{
    init_nvs();
    s_cmd_lock = xSemaphoreCreateMutex();
    tfw_regwin_init(&g_win, BUILD_IRAM ? TFW_CFG_PCNT_IRAM : 0);
    cfg_load();
    memset((void *)&g_res, 0, sizeof(g_res));

    esp_chip_info_t ci;
    esp_chip_info(&ci);
    uint32_t hz = 0;
    esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &hz);
    s_cpu_mhz = hz ? hz / 1000000u : CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    g_res.chip_rev = ci.revision;
    g_res.cpu_mhz = (uint16_t)s_cpu_mhz;
    g_res.mb_max_clients = CONFIG_TFW_MAX_CLIENTS;
    tfw_pulse2_set_cpu_mhz(s_cpu_mhz);          /* m-02: measured clock, not the Kconfig default */
    tfw_estop_set_cpu_mhz(s_cpu_mhz);
    const bool core_b = (g_win.cfg_flags & TFW_CFG_CORE_B) != 0;
    s_core_upd = core_b ? 0 : 1;               /* layout A: update core 1, sequencer/COMM core 0 [assumed]; B reversed */
    s_core_comm = core_b ? 1 : 0;

    ESP_LOGI(TAG, "==== TFW P4 pre-test firmware (D-025) ====");
    ESP_LOGI(TAG, "chip model=%d cores=%d revision=%u cpu=%lu MHz", (int)ci.model, (int)ci.cores, (unsigned)ci.revision, (unsigned long)s_cpu_mhz);
    ESP_LOGI(TAG, "config: res=%lu Hz upd=%u us mode=%s flags=0x%x layout=%c axes=%d", (unsigned long)g_win.cfg_res_hz,
             (unsigned)g_win.cfg_upd_us, TFW_B_UPD_MODE_HWTIMER ? "HWTIMER" : "TASK_10MS", (unsigned)g_win.cfg_flags,
             core_b ? 'B' : 'A', CONFIG_TFW_AXIS_COUNT);
    ESP_LOGI(TAG, "IRAM options: PCNT_ISR_IRAM_SAFE=%d PCNT_CTRL_FUNC_IN_IRAM=%d MCPWM_ISR_IRAM_SAFE=%d MCPWM_CTRL_FUNC_IN_IRAM=%d GPTIMER_ISR_IRAM_SAFE=%d",
#if CONFIG_PCNT_ISR_IRAM_SAFE
             1,
#else
             0,
#endif
#if CONFIG_PCNT_CTRL_FUNC_IN_IRAM
             1,
#else
             0,
#endif
#if CONFIG_MCPWM_ISR_IRAM_SAFE
             1,
#else
             0,
#endif
#if CONFIG_MCPWM_CTRL_FUNC_IN_IRAM
             1,
#else
             0,
#endif
#if CONFIG_GPTIMER_ISR_IRAM_SAFE
             1
#else
             0
#endif
             );
    ESP_LOGI(TAG, "pins: AX1 PLS=%d DIR=%d EN=%d | AX2 PLS=%d DIR=%d EN=%d | ESTOP=%d active %s | dbg=%d/%d | probe=%d",
             CONFIG_TFW_AX1_PLS_GPIO, CONFIG_TFW_AX1_DIR_GPIO, CONFIG_TFW_AX1_EN_GPIO, CONFIG_TFW_AX2_PLS_GPIO,
             CONFIG_TFW_AX2_DIR_GPIO, CONFIG_TFW_AX2_EN_GPIO, CONFIG_TFW_ESTOP_GPIO,
             TFW_B_ESTOP_ACTIVE_LOW ? "LOW" : "HIGH", CONFIG_TFW_DEBUG_GPIO, CONFIG_TFW_DEBUG_GPIO_ESTOP, CONFIG_TFW_PROBE_GPIO);
    ESP_LOGI(TAG, "legacy NVS-on-write=%d (servo1axis behaviour; default off); max_clients=%d (reg 0x705E); irq levels estop=%d pcnt=%d gpt=%d",
             TFW_B_LEGACY_NVS_ON_WRITE, CONFIG_TFW_MAX_CLIENTS, CONFIG_TFW_IRQ_PRIO_ESTOP, CONFIG_TFW_IRQ_PRIO_PCNT, CONFIG_TFW_IRQ_PRIO_GPT);

    tfw_caps_t caps;
    tfw_pulse2_probe_caps(&caps);

    tfw_pulse2_set_update_core(s_core_upd);
    init_arg_t ia = {.done = xSemaphoreCreateBinary(), .err = ESP_FAIL};
    xTaskCreatePinnedToCore(pulse_init_task, "tfw_init", 4096, &ia, 10, NULL, s_core_upd);
    xSemaphoreTake(ia.done, portMAX_DELAY);
    if (ia.err != ESP_OK) ESP_LOGE(TAG, "tfw_pulse2_init failed 0x%x - axis 1 unavailable", (unsigned)ia.err);
    if (CONFIG_TFW_AXIS_COUNT == 2 && !tfw_pulse2_axis_ok(1))
        ESP_LOGE(TAG, "AXIS 2 UNAVAILABLE (err 0x%x): RUN commands with axis 2 return tst_status 0x12", (unsigned)tfw_pulse2_axis_err(1));

    mcpwm_oper_handle_t opers[2] = {tfw_pulse2_operator(0), tfw_pulse2_operator(1)};
    esp_err_t ee = tfw_estop_init(CONFIG_TFW_ESTOP_GPIO, TFW_B_ESTOP_ACTIVE_LOW, opers);
    if (ee != ESP_OK) ESP_LOGE(TAG, "tfw_estop_init failed 0x%x: ALL motion commands are refused (0x10) - N-02", (unsigned)ee);
    tfw_pulse2_set_block_fn(tfw_estop_blocking);
    if (!(g_win.cfg_flags & TFW_CFG_BRAKE_USE)) { tfw_estop_set_path(1); g_win.es_path_sel = 1; }
    else g_win.es_path_sel = 3;

    const tfw_mb_hooks_t hooks = {.win = &g_win, .get_results = hook_results, .on_effect = on_effect, .user = NULL, .seq_busy = hook_seq_busy};
    tfw_modbus_set_hooks(&hooks);
    tfw_modbus_set_core(s_core_comm);
    const uint8_t ip[4] = {CONFIG_TFW_IP_A, CONFIG_TFW_IP_B, CONFIG_TFW_IP_C, CONFIG_TFW_IP_D};
    ee = tfw_modbus_start(ip, CONFIG_TFW_PORT, CONFIG_TFW_UNIT_ID, CONFIG_TFW_MAX_CLIENTS);
    if (ee != ESP_OK) ESP_LOGE(TAG, "tfw_modbus_start failed 0x%x", (unsigned)ee);

    xTaskCreatePinnedToCore(seq_task, "tfw_seq", 4096, NULL, 8, &s_seq_task, s_core_comm);
    xTaskCreate(nvs_load_task, "tfw_nvs", 4096, NULL, tskIDLE_PRIORITY + 1, &s_nvs_task);
    xTaskCreatePinnedToCore(cpu_load_task, "tfw_cpu", 2048, NULL, tskIDLE_PRIORITY + 1, NULL, s_core_upd);

    ESP_LOGI(TAG, "ready: window 0x7000-0x70FF at %u.%u.%u.%u:%u unit %u", ip[0], ip[1], ip[2], ip[3], CONFIG_TFW_PORT, CONFIG_TFW_UNIT_ID);
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
