/* fw_hal pulse engine for ESP32-P4: one MCPWM group (timer + operator + comparator + generator) per axis, one PCNT
 * unit per axis counting the generator's own output pin.
 * Source: project/04_code/test-fw/components/tfw_pulse2/tfw_pulse2.c (reviewed, M-01..M-08, N-03 applied), itself
 * derived from servo1axis motion_core.c. Removed for the product: resource probe, statistics, debug GPIO, fixed/endless
 * run, the GPTimer update mode (profile updates are done by the fw_motion task, in task context: no FPU in an ISR,
 * review M-02), global pulse-enable handling (see hal_io/hal_sys: GPIO33 is owned by fw_hal_oe_*).
 * NOT verified on hardware (no ESP-IDF / board in the authoring container). */
#include <string.h>
#include "sdkconfig.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "fw_cfg.h"
#include "fw_hal.h"
#include "fw_profile.h"
#include "port_internal.h"

static const char *TAG = "FW_PULSE";

typedef struct {
    bool ok;
    int pls_gpio, dir_gpio;
    uint32_t res_hz;
    mcpwm_timer_handle_t timer;
    mcpwm_oper_handle_t oper;
    mcpwm_cmpr_handle_t cmpr;
    mcpwm_gen_handle_t gen;
    pcnt_unit_handle_t pcnt;
    pcnt_channel_handle_t chan;
    /* ISR-shared state (DRAM) */
    volatile uint32_t completed;
    volatile uint32_t target;
    volatile bool active, done;
    uint32_t last_total, cur_ticks;
    int wp_target;
    bool wp_high;
} axis_t;

static axis_t s_ax[FW_MAX_AXES_HAL];
static uint8_t s_axis_count;   /* kept for diagnostics */

static inline bool IRAM_ATTR set_ticks(axis_t *a, uint32_t f_hz)
{
    uint32_t ticks;
    if (!fw_ticks_for_freq(a->res_hz, f_hz, FW_MAX_PERIOD_TICKS, &ticks)) return false;     /* keep the previous period */
    if (ticks != a->cur_ticks) {
        mcpwm_timer_set_period(a->timer, ticks);
        mcpwm_comparator_set_compare_value(a->cmpr, fw_cmp_for_ticks(ticks));
        a->cur_ticks = ticks;
    }
    return true;
}

static uint32_t IRAM_ATTR axis_total(axis_t *a)
{
    uint32_t c1, c2;
    int n = 0;
    do {
        c1 = a->completed;
        pcnt_unit_get_count(a->pcnt, &n);
        c2 = a->completed;
    } while (c1 != c2);
    uint32_t t = fw_total_count(c1, n);
    if (t < a->last_total) t = a->last_total; else a->last_total = t;       /* never backwards (block wrap before the ISR) */
    return t;
}

/* PCNT watch point ISR: IRAM, DRAM statics only, driver calls that are IRAM-safe, NO RTOS API (review M-01) */
static bool IRAM_ATTR on_reach(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *e, void *ctx)
{
    (void)unit;
    axis_t *a = (axis_t *)ctx;
    uint32_t total;
    if (e->watch_point_value == (int)FW_PCNT_HIGH_LIMIT) {
        a->completed += FW_PCNT_HIGH_LIMIT;
        total = a->completed;
    } else {
        total = a->completed + (uint32_t)e->watch_point_value;
    }
    if (a->active && total >= a->target) {
        mcpwm_generator_set_force_level(a->gen, 0, true);
        a->done = true;
    }
    return false;
}


static void destroy_axis(axis_t *a)
{
    if (a->timer) mcpwm_timer_disable(a->timer);
    if (a->chan) pcnt_del_channel(a->chan);
    if (a->pcnt) { pcnt_unit_disable(a->pcnt); pcnt_del_unit(a->pcnt); }
    if (a->gen) mcpwm_del_generator(a->gen);
    if (a->cmpr) mcpwm_del_comparator(a->cmpr);
    if (a->oper) mcpwm_del_operator(a->oper);
    if (a->timer) mcpwm_del_timer(a->timer);
    a->chan = NULL; a->pcnt = NULL; a->gen = NULL; a->cmpr = NULL; a->oper = NULL; a->timer = NULL;
    a->ok = false;
}

#define STEP(expr, what) do { esp_err_t _e = (expr); if (_e != ESP_OK) { \
    ESP_LOGE(TAG, "axis %u: %s failed: %s (0x%x)", (unsigned)(idx + 1), what, esp_err_to_name(_e), (unsigned)_e); \
    destroy_axis(a); return _e; } } while (0)

static esp_err_t create_axis(uint8_t idx, int pls, int dir, uint32_t res_hz)
{
    axis_t *a = &s_ax[idx];
    memset(a, 0, sizeof(*a));
    a->pls_gpio = pls; a->dir_gpio = dir; a->res_hz = res_hz;

    gpio_config_t outs = {.pin_bit_mask = (1ULL << dir), .mode = GPIO_MODE_OUTPUT};
    STEP(gpio_config(&outs), "dir gpio");
    gpio_set_level(dir, 1);

    mcpwm_timer_config_t tc = {
        .group_id = idx,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = res_hz,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
        .period_ticks = res_hz / 1000,
        /* TEZ update keeps a period change glitch free while running. Whether a period written while STOPPED is used by
         * the first cycle after the start is unverified (test-fw review M-05): CONFIG_FW_PERIOD_IMMEDIATE=y selects the
         * servo1axis behaviour (immediate update) if the first cycle is wrong. */
        .flags.update_period_on_empty = !FW_B_PERIOD_IMMEDIATE,
    };
    STEP(mcpwm_new_timer(&tc, &a->timer), "mcpwm timer");
    mcpwm_operator_config_t oc = {.group_id = idx};
    STEP(mcpwm_new_operator(&oc, &a->oper), "mcpwm operator");
    STEP(mcpwm_operator_connect_timer(a->oper, a->timer), "connect timer");
    mcpwm_comparator_config_t cc = {.flags.update_cmp_on_tez = true};
    STEP(mcpwm_new_comparator(a->oper, &cc, &a->cmpr), "comparator");
    mcpwm_generator_config_t gc = {.gen_gpio_num = pls};
    STEP(mcpwm_new_generator(a->oper, &gc, &a->gen), "generator");
    STEP(mcpwm_generator_set_action_on_timer_event(a->gen,
         MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH)), "timer action");
    STEP(mcpwm_generator_set_action_on_compare_event(a->gen,
         MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, a->cmpr, MCPWM_GEN_ACTION_LOW)), "compare action");
    STEP(mcpwm_timer_enable(a->timer), "timer enable");
    mcpwm_generator_set_force_level(a->gen, 0, true);              /* output LOW until a move starts */

    pcnt_unit_config_t uc = {.low_limit = -1, .high_limit = (int)FW_PCNT_HIGH_LIMIT, .flags.accum_count = false,
                             .intr_priority = CONFIG_FW_IRQ_PRIO_PCNT};
    STEP(pcnt_new_unit(&uc, &a->pcnt), "pcnt unit");
    pcnt_chan_config_t pc = {.edge_gpio_num = pls, .level_gpio_num = -1};
    STEP(pcnt_new_channel(a->pcnt, &pc, &a->chan), "pcnt channel");
    STEP(pcnt_channel_set_edge_action(a->chan, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD), "pcnt edge");
    pcnt_event_callbacks_t cb = {.on_reach = on_reach};
    STEP(pcnt_unit_register_event_callbacks(a->pcnt, &cb, a), "pcnt callback");
    STEP(pcnt_unit_enable(a->pcnt), "pcnt enable");
    a->wp_target = -1;
    a->ok = true;
    ESP_LOGI(TAG, "axis %u ready: MCPWM group %u, PLS=%d DIR=%d, resolution %lu Hz", (unsigned)(idx + 1), (unsigned)idx, pls, dir,
             (unsigned long)res_hz);
    return ESP_OK;
}

int fw_hal_pulse_init(uint8_t axis_count, const uint32_t res_hz[2])
{
    if (axis_count < 1 || axis_count > FW_MAX_AXES_HAL) return FW_PE_BAD_ARG;
    s_axis_count = axis_count; (void)s_axis_count;
    /* pin set: every pin the firmware uses must be unique (review M-08) */
    const int pins[] = {CONFIG_FW_X_PLS_GPIO, CONFIG_FW_X_DIR_GPIO, CONFIG_FW_Y_PLS_GPIO, CONFIG_FW_Y_DIR_GPIO,
                        CONFIG_FW_OE_GPIO, CONFIG_FW_X_START_GPIO, CONFIG_FW_X_STOP_GPIO, CONFIG_FW_X_DI3_GPIO, CONFIG_FW_X_DI4_GPIO,
                        CONFIG_FW_Y_START_GPIO, CONFIG_FW_Y_STOP_GPIO, CONFIG_FW_Y_DI7_GPIO, CONFIG_FW_Y_DI8_GPIO,
                        CONFIG_FW_DO1_GPIO, CONFIG_FW_DO2_GPIO, CONFIG_FW_DO3_GPIO, CONFIG_FW_DO4_GPIO,
                        CONFIG_FW_RS485_TX_GPIO, CONFIG_FW_RS485_RX_GPIO, CONFIG_FW_RS485_DE_GPIO, CONFIG_FW_PHY_RESET_GPIO};
    const int n = (int)(sizeof(pins) / sizeof(pins[0]));
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            if (pins[i] >= 0 && pins[i] == pins[j]) {
                ESP_LOGE(TAG, "GPIO%d is configured for two functions: pulse engine NOT created", pins[i]);
                return FW_PE_FAIL;
            }
    const int pls[2] = {CONFIG_FW_X_PLS_GPIO, CONFIG_FW_Y_PLS_GPIO};
    const int dir[2] = {CONFIG_FW_X_DIR_GPIO, CONFIG_FW_Y_DIR_GPIO};
    int rc = FW_PE_OK;
    for (uint8_t i = 0; i < axis_count; ++i) {
        if (!fw_res_valid(res_hz[i])) { ESP_LOGE(TAG, "axis %u: resolution %lu Hz invalid", (unsigned)(i + 1), (unsigned long)res_hz[i]); rc = FW_PE_BAD_ARG; continue; }
        if (create_axis(i, pls[i], dir[i], res_hz[i]) != ESP_OK) rc = FW_PE_FAIL;
    }
    return rc;
}

bool fw_hal_pulse_ready(uint8_t axis) { return axis < FW_MAX_AXES_HAL && s_ax[axis].ok; }
mcpwm_gen_handle_t fw_port_generator(uint8_t axis) { return axis < FW_MAX_AXES_HAL ? s_ax[axis].gen : NULL; }
mcpwm_oper_handle_t fw_port_operator(uint8_t axis) { return axis < FW_MAX_AXES_HAL ? s_ax[axis].oper : NULL; }
void fw_hal_dir_set(uint8_t axis, bool level_high) { if (fw_hal_pulse_ready(axis)) gpio_set_level(s_ax[axis].dir_gpio, level_high ? 1 : 0); }
bool fw_hal_pulse_active(uint8_t axis) { return axis < FW_MAX_AXES_HAL && s_ax[axis].active; }

static void remove_wps(axis_t *a)
{
    if (a->wp_target > 0) pcnt_unit_remove_watch_point(a->pcnt, a->wp_target);
    if (a->wp_high) pcnt_unit_remove_watch_point(a->pcnt, (int)FW_PCNT_HIGH_LIMIT);
    a->wp_target = -1; a->wp_high = false;
}

int fw_hal_pulse_start(uint8_t axis, uint32_t first_hz, uint32_t target)
{
    if (!fw_hal_pulse_ready(axis) || !target) return FW_PE_BAD_ARG;
    axis_t *a = &s_ax[axis];
    if (a->active) return FW_PE_BUSY;
    /* N-03: no start while the STOP is latched/active/not initialised */
    if (fw_port_stop_blocking(axis)) return FW_PE_STOP_BLOCK;
    uint32_t ticks;
    if (!fw_ticks_for_freq(a->res_hz, first_hz, FW_MAX_PERIOD_TICKS, &ticks)) return FW_PE_BAD_ARG;

    int rc = FW_PE_FAIL;
    pcnt_unit_stop(a->pcnt);
    remove_wps(a);
    a->completed = 0; a->last_total = 0; a->target = target; a->done = false;
    if (pcnt_unit_add_watch_point(a->pcnt, (int)FW_PCNT_HIGH_LIMIT) != ESP_OK) goto fail;
    a->wp_high = true;
    a->wp_target = (int)fw_target_remainder(target);
    if (a->wp_target > 0 && pcnt_unit_add_watch_point(a->pcnt, a->wp_target) != ESP_OK) { a->wp_target = -1; goto fail; }
    if (pcnt_unit_clear_count(a->pcnt) != ESP_OK) goto fail;       /* new watch points become active after a clear */
    if (pcnt_unit_start(a->pcnt) != ESP_OK) goto fail;
    a->cur_ticks = 0;                                              /* force the period write */
    if (!set_ticks(a, first_hz)) goto fail;
    a->active = true;
    if (mcpwm_generator_set_force_level(a->gen, -1, true) != ESP_OK) goto fail;
    if (mcpwm_timer_start_stop(a->timer, MCPWM_TIMER_START_NO_STOP) != ESP_OK) goto fail;
    /* N-03: the STOP ISR latches BEFORE it stops, so the latch is visible here even if it ran between "active = true"
     * and the timer start: stop again and report the block */
    if (fw_port_stop_blocking(axis)) { fw_hal_pulse_stop(axis); return FW_PE_STOP_BLOCK; }
    return FW_PE_OK;
fail:
    a->active = false;
    mcpwm_generator_set_force_level(a->gen, 0, true);
    mcpwm_timer_start_stop(a->timer, MCPWM_TIMER_STOP_EMPTY);
    pcnt_unit_stop(a->pcnt);
    remove_wps(a);
    return rc;
}

int fw_hal_pulse_set_freq(uint8_t axis, uint32_t hz)
{
    if (!fw_hal_pulse_ready(axis)) return FW_PE_BAD_ARG;
    return set_ticks(&s_ax[axis], hz) ? FW_PE_OK : FW_PE_BAD_ARG;
}

void IRAM_ATTR fw_hal_pulse_stop(uint8_t axis)
{
    if (axis >= FW_MAX_AXES_HAL) return;
    axis_t *a = &s_ax[axis];
    if (!a->ok) return;
    a->active = false;
    mcpwm_generator_set_force_level(a->gen, 0, true);
    mcpwm_timer_start_stop(a->timer, MCPWM_TIMER_STOP_EMPTY);
}

bool fw_hal_pulse_finished(uint8_t axis) { return axis < FW_MAX_AXES_HAL && s_ax[axis].ok && s_ax[axis].active && s_ax[axis].done; }

void fw_hal_pulse_end(uint8_t axis)
{
    if (!fw_hal_pulse_ready(axis)) return;
    axis_t *a = &s_ax[axis];
    mcpwm_generator_set_force_level(a->gen, 0, true);
    mcpwm_timer_start_stop(a->timer, MCPWM_TIMER_STOP_EMPTY);
    pcnt_unit_stop(a->pcnt);
    a->active = false;
}

uint32_t IRAM_ATTR fw_port_count_isr(uint8_t axis)
{
    if (axis >= FW_MAX_AXES_HAL || !s_ax[axis].ok) return 0;
    return axis_total(&s_ax[axis]);
}

uint32_t fw_hal_pulse_count(uint8_t axis) { return fw_port_count_isr(axis); }
