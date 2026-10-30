/* TFW-02 tfw_pulse2.
 * Based on servo1axis components/motion_core/motion_core.c [E1] (MCPWM timer/operator/comparator/generator
 * setup, PCNT 30,000-block accumulation, watch points) and calculate_profile/profile_frequency [E2]
 * (now in tfw_logic). NOT verified on hardware (no ESP-IDF / board in the authoring container).
 *
 * Changes versus the original, made on purpose:
 *  risk 1 (2 MHz resolution, output above request): period = ceil(res/f) via tfw_ticks_for_freq();
 *         requests that need more than max_period_ticks are rejected, never clamped.
 *  risk 2 (PCNT ISR IRAM safety): callbacks are IRAM_ATTR, ISR touches only DRAM statics, calls only
 *         IRAM-safe driver functions and NO RTOS API (completion is a volatile flag polled by the task).
 *         The IRAM-safe driver options are in sdkconfig.tfw_iram_on (see notes/test-fw.md).
 *  per-axis state arrays instead of file-static singletons; MCPWM group = axis index.
 */
#include "tfw_pulse2.h"
#include <string.h>
#include "sdkconfig.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_cpu.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "driver/gptimer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tfw_logic.h"

static const char *TAG = "TFW_PULSE2";

#define MAX_AXES 2
#define NO_GPIO (-1)
#if CONFIG_TFW_DEBUG_GPIO >= 0
#define DBG_TOGGLE() gpio_set_level(CONFIG_TFW_DEBUG_GPIO, 1), gpio_set_level(CONFIG_TFW_DEBUG_GPIO, 0)
#else
#define DBG_TOGGLE() ((void)0)
#endif

typedef struct {
    bool ok;
    int err;
    int pls_gpio, dir_gpio, en_gpio;
    mcpwm_timer_handle_t timer;
    mcpwm_oper_handle_t oper;
    mcpwm_cmpr_handle_t cmpr;
    mcpwm_gen_handle_t gen;
    pcnt_unit_handle_t pcnt;
    pcnt_channel_handle_t chan;
    /* ISR-shared state (DRAM) */
    volatile uint32_t completed;
    volatile uint32_t target;
    volatile bool active, done, fixed;
    uint32_t last_total;
    uint32_t cur_ticks;
    uint32_t freq_actual;
    volatile uint32_t isr_lat_ns_max;
    int wp_target;
    bool wp_high;
    tfw_profile_t prof;
} axis_t;

static axis_t s_ax[MAX_AXES];
static uint8_t s_axis_count;
static uint32_t s_res_hz, s_upd_us, s_ns_per_tick, s_cpu_mhz;
static tfw_upd_mode_t s_mode;
static tfw_upd_stats_t s_upd;
static gptimer_handle_t s_gpt;
static TaskHandle_t s_upd_task;
static int s_upd_core = -1;
static uint32_t s_max_ticks = TFW_MAX_PERIOD_TICKS;   /* lowered by probe_caps if the driver accepts less */

/* ------------------------------------------------------------------ ISR-side helpers */
static inline bool IRAM_ATTR set_ticks(axis_t *a, uint32_t f_hz)
{
    uint32_t ticks;
    if (!tfw_ticks_for_freq(s_res_hz, f_hz, s_max_ticks, &ticks)) return false;   /* keep previous period */
    if (ticks != a->cur_ticks) {
        /* timer flag update_period_on_empty and comparator update_cmp_on_tez apply both at the next TEZ */
        mcpwm_timer_set_period(a->timer, ticks);
        mcpwm_comparator_set_compare_value(a->cmpr, tfw_cmp_for_ticks(ticks));
        a->cur_ticks = ticks;
        a->freq_actual = tfw_freq_actual(s_res_hz, ticks);
    }
    return true;
}

static inline uint32_t IRAM_ATTR axis_total(axis_t *a)
{
    uint32_t c1, c2;
    int n = 0;
    do {
        c1 = a->completed;
        pcnt_unit_get_count(a->pcnt, &n);
        c2 = a->completed;
    } while (c1 != c2);
    uint32_t t = tfw_total_count(c1, n);
    /* A block wrap seen before the watch-point ISR added 30,000 would look like a huge step back
       (the original code has the same transient). Never let the total go backwards. */
    if (t < a->last_total) t = a->last_total; else a->last_total = t;
    return t;
}

static bool IRAM_ATTR on_reach(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *e, void *ctx)
{
    axis_t *a = (axis_t *)ctx;
    uint32_t total, base;
    if (e->watch_point_value == (int)TFW_PCNT_HIGH_LIMIT) {
        a->completed += TFW_PCNT_HIGH_LIMIT;
        total = a->completed;
        base = 0;
    } else {
        total = a->completed + (uint32_t)e->watch_point_value;
        base = (uint32_t)e->watch_point_value;
    }
    if (a->active && total >= a->target) {
        mcpwm_generator_set_force_level(a->gen, 0, true);
        DBG_TOGGLE();
        int now = 0;
        pcnt_unit_get_count(unit, &now);
        uint32_t past = ((uint32_t)now > base) ? (uint32_t)now - base : 0;
        uint32_t lat = past * a->cur_ticks * s_ns_per_tick;
        if (lat > a->isr_lat_ns_max) a->isr_lat_ns_max = lat;
        a->done = true;
    }
    return false;     /* no RTOS wake-up from this ISR */
}

static void IRAM_ATTR update_all(void)
{
    uint32_t c0 = esp_cpu_get_cycle_count();
    for (uint8_t i = 0; i < s_axis_count; ++i) {
        axis_t *a = &s_ax[i];
        if (a->ok && a->active && !a->done && !a->fixed) {
            uint32_t n = axis_total(a);
            set_ticks(a, tfw_profile_freq(&a->prof, n));
        }
    }
    uint32_t c1 = esp_cpu_get_cycle_count();
    tfw_upd_record(&s_upd, c0, c1, s_cpu_mhz, s_upd_us);
}

static bool IRAM_ATTR on_gpt_alarm(gptimer_handle_t t, const gptimer_alarm_event_data_t *e, void *u)
{
    (void)t; (void)e; (void)u;
    update_all();
    return false;
}

static void update_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        update_all();
        vTaskDelayUntil(&last, pdMS_TO_TICKS(10));
    }
}

/* ------------------------------------------------------------------ axis lifetime */
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
}

#define STEP(expr, what) do { esp_err_t _e = (expr); if (_e != ESP_OK) { \
    ESP_LOGE(TAG, "axis %u: %s failed: %s (0x%x)", (unsigned)idx, what, esp_err_to_name(_e), (unsigned)_e); \
    a->err = (int)_e; destroy_axis(a); return _e; } } while (0)

static esp_err_t create_axis(uint8_t idx, int pls, int dir, int en)
{
    axis_t *a = &s_ax[idx];
    memset(a, 0, sizeof(*a));
    a->pls_gpio = pls; a->dir_gpio = dir; a->en_gpio = en;

    gpio_config_t outs = {.pin_bit_mask = (1ULL << dir) | (1ULL << en), .mode = GPIO_MODE_OUTPUT};
    STEP(gpio_config(&outs), "dir/en gpio");
    gpio_set_level(en, 0);
    gpio_set_level(dir, 1);

    mcpwm_timer_config_t tc = {
        .group_id = idx,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = s_res_hz,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
        .period_ticks = s_res_hz / 1000,
        .flags.update_period_on_empty = true,
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
    mcpwm_generator_set_force_level(a->gen, 0, true);

    pcnt_unit_config_t uc = {.low_limit = -1, .high_limit = (int)TFW_PCNT_HIGH_LIMIT, .flags.accum_count = false};
    STEP(pcnt_new_unit(&uc, &a->pcnt), "pcnt unit");
    pcnt_chan_config_t pc = {.edge_gpio_num = pls, .level_gpio_num = -1};
    STEP(pcnt_new_channel(a->pcnt, &pc, &a->chan), "pcnt channel");
    STEP(pcnt_channel_set_edge_action(a->chan, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD), "pcnt edge");
    pcnt_event_callbacks_t cb = {.on_reach = on_reach};
    STEP(pcnt_unit_register_event_callbacks(a->pcnt, &cb, a), "pcnt callback");
    STEP(pcnt_unit_enable(a->pcnt), "pcnt enable");
    a->wp_target = -1;
    a->ok = true;
    ESP_LOGI(TAG, "axis %u ready: MCPWM group %u, PCNT unit, PLS=%d DIR=%d EN=%d", (unsigned)(idx + 1), (unsigned)idx, pls, dir, en);
    return ESP_OK;
}

esp_err_t tfw_pulse2_init(uint8_t axis_count, uint32_t res_hz, uint32_t upd_us, tfw_upd_mode_t mode)
{
    ESP_RETURN_ON_FALSE(axis_count >= 1 && axis_count <= MAX_AXES, ESP_ERR_INVALID_ARG, TAG, "axis_count");
    ESP_RETURN_ON_FALSE(tfw_res_valid(res_hz), ESP_ERR_INVALID_ARG, TAG, "resolution %lu not in {2,10,20,40} MHz", (unsigned long)res_hz);
    ESP_RETURN_ON_FALSE(upd_us >= 500 && upd_us <= 10000, ESP_ERR_INVALID_ARG, TAG, "update period");
    s_res_hz = res_hz; s_upd_us = upd_us; s_mode = mode;
    s_ns_per_tick = 1000000000u / res_hz;
    s_cpu_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    s_axis_count = axis_count;
    memset(&s_upd, 0, sizeof(s_upd));
#if CONFIG_TFW_DEBUG_GPIO >= 0
    gpio_config_t dbg = {.pin_bit_mask = 1ULL << CONFIG_TFW_DEBUG_GPIO, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&dbg);
#endif
    ESP_LOGI(TAG, "res=%lu Hz upd=%lu us mode=%s max_period_ticks=%u min_freq=%lu Hz (below this requests are rejected)",
             (unsigned long)res_hz, (unsigned long)upd_us, mode == TFW_UPD_HWTIMER ? "HWTIMER" : "TASK_10MS",
             (unsigned)s_max_ticks, (unsigned long)(res_hz / s_max_ticks + 1));

    esp_err_t e = create_axis(0, CONFIG_TFW_AX1_PLS_GPIO, CONFIG_TFW_AX1_DIR_GPIO, CONFIG_TFW_AX1_EN_GPIO);
    if (e != ESP_OK) return e;                    /* axis 1 is mandatory */
    if (axis_count == 2) {
        esp_err_t e2 = create_axis(1, CONFIG_TFW_AX2_PLS_GPIO, CONFIG_TFW_AX2_DIR_GPIO, CONFIG_TFW_AX2_EN_GPIO);
        if (e2 != ESP_OK) ESP_LOGW(TAG, "axis 2 NOT available (esp_err 0x%x): group 1 / PCNT unit 2 / pins -> tst_status 0x12", (unsigned)e2);
    }

    if (mode == TFW_UPD_HWTIMER) {
        gptimer_config_t gc = {.clk_src = GPTIMER_CLK_SRC_DEFAULT, .direction = GPTIMER_COUNT_UP, .resolution_hz = 1000000};
        ESP_RETURN_ON_ERROR(gptimer_new_timer(&gc, &s_gpt), TAG, "gptimer");
        gptimer_alarm_config_t al = {.alarm_count = upd_us, .reload_count = 0, .flags.auto_reload_on_alarm = true};
        ESP_RETURN_ON_ERROR(gptimer_set_alarm_action(s_gpt, &al), TAG, "gptimer alarm");
        gptimer_event_callbacks_t cbs = {.on_alarm = on_gpt_alarm};
        ESP_RETURN_ON_ERROR(gptimer_register_event_callbacks(s_gpt, &cbs, NULL), TAG, "gptimer cb");
        ESP_RETURN_ON_ERROR(gptimer_enable(s_gpt), TAG, "gptimer enable");   /* interrupt is allocated on the calling core */
        ESP_RETURN_ON_ERROR(gptimer_start(s_gpt), TAG, "gptimer start");
    } else {
        BaseType_t ok = (s_upd_core >= 0)
            ? xTaskCreatePinnedToCore(update_task, "tfw_upd", 3072, NULL, 10, &s_upd_task, s_upd_core)
            : xTaskCreate(update_task, "tfw_upd", 3072, NULL, 10, &s_upd_task);
        ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "update task");
    }
    return ESP_OK;
}

void tfw_pulse2_set_update_core(int core) { s_upd_core = core; }
bool tfw_pulse2_axis_ok(uint8_t axis) { return axis < MAX_AXES && s_ax[axis].ok; }
int  tfw_pulse2_axis_err(uint8_t axis) { return axis < MAX_AXES ? s_ax[axis].err : ESP_ERR_INVALID_ARG; }
mcpwm_gen_handle_t  tfw_pulse2_generator(uint8_t axis) { return axis < MAX_AXES ? s_ax[axis].gen : NULL; }
mcpwm_oper_handle_t tfw_pulse2_operator(uint8_t axis)  { return axis < MAX_AXES ? s_ax[axis].oper : NULL; }
uint32_t tfw_pulse2_res_hz(void) { return s_res_hz; }

void tfw_pulse2_set_dir(uint8_t axis, bool forward)
{
    if (axis < MAX_AXES && s_ax[axis].ok) gpio_set_level(s_ax[axis].dir_gpio, forward ? 1 : 0);
}

/* ------------------------------------------------------------------ run control */
static void remove_wps(axis_t *a)
{
    if (a->wp_target > 0) pcnt_unit_remove_watch_point(a->pcnt, a->wp_target);
    if (a->wp_high) pcnt_unit_remove_watch_point(a->pcnt, (int)TFW_PCNT_HIGH_LIMIT);
    a->wp_target = -1; a->wp_high = false;
}

static esp_err_t start_common(axis_t *a, uint32_t first_freq, uint32_t target, bool fixed)
{
    if (!a->ok) return ESP_ERR_INVALID_STATE;
    if (a->active) return ESP_ERR_INVALID_STATE;
    uint32_t ticks;
    if (!tfw_ticks_for_freq(s_res_hz, first_freq, s_max_ticks, &ticks)) return ESP_ERR_INVALID_ARG;

    pcnt_unit_stop(a->pcnt);                      /* may already be stopped */
    remove_wps(a);
    a->completed = 0; a->last_total = 0; a->target = target; a->done = false; a->fixed = fixed;
    esp_err_t e = pcnt_unit_add_watch_point(a->pcnt, (int)TFW_PCNT_HIGH_LIMIT);
    if (e != ESP_OK) goto fail;
    a->wp_high = true;
    a->wp_target = (int)tfw_target_remainder(target);
    if (a->wp_target > 0) {
        e = pcnt_unit_add_watch_point(a->pcnt, a->wp_target);
        if (e != ESP_OK) { a->wp_target = -1; goto fail; }
    }
    e = pcnt_unit_clear_count(a->pcnt);           /* new watch points become active after a clear */
    if (e != ESP_OK) goto fail;
    e = pcnt_unit_start(a->pcnt);
    if (e != ESP_OK) goto fail;
    a->cur_ticks = 0;                             /* force a write */
    {
        axis_t *p = a;
        if (!set_ticks(p, first_freq)) { e = ESP_ERR_INVALID_ARG; goto fail; }
    }
    gpio_set_level(a->en_gpio, 1);
    a->active = true;
    e = mcpwm_generator_set_force_level(a->gen, -1, true);
    if (e != ESP_OK) goto fail;
    e = mcpwm_timer_start_stop(a->timer, MCPWM_TIMER_START_NO_STOP);
    if (e == ESP_OK) return ESP_OK;
fail:
    a->active = false;
    mcpwm_generator_set_force_level(a->gen, 0, true);
    mcpwm_timer_start_stop(a->timer, MCPWM_TIMER_STOP_EMPTY);
    pcnt_unit_stop(a->pcnt);
    remove_wps(a);
    gpio_set_level(a->en_gpio, 0);
    return e;
}

esp_err_t tfw_pulse2_move(uint8_t axis, uint32_t speed_hz, uint32_t count, uint32_t accel_ms, uint32_t decel_ms)
{
    if (axis >= MAX_AXES || !count || speed_hz < 100 || speed_hz > 1000000u) return ESP_ERR_INVALID_ARG;
    axis_t *a = &s_ax[axis];
    if (a->active) return ESP_ERR_INVALID_STATE;
    const uint32_t start = tfw_start_speed(speed_hz);
    tfw_profile_calc(&a->prof, count, start, speed_hz, accel_ms, decel_ms);   /* before active: ISR does not read it yet */
    return start_common(a, start, count, false);
}

esp_err_t tfw_pulse2_run_fixed(uint8_t axis, uint32_t freq_hz, uint32_t count)
{
    if (axis >= MAX_AXES || freq_hz < 100 || freq_hz > 1000000u) return ESP_ERR_INVALID_ARG;
    return start_common(&s_ax[axis], freq_hz, count ? count : UINT32_MAX, true);
}

void IRAM_ATTR tfw_pulse2_stop(uint8_t axis)
{
    if (axis >= MAX_AXES) return;
    axis_t *a = &s_ax[axis];
    if (!a->ok) return;
    a->active = false;
    mcpwm_generator_set_force_level(a->gen, 0, true);
    mcpwm_timer_start_stop(a->timer, MCPWM_TIMER_STOP_EMPTY);
    /* PCNT is left running on purpose: TFW-04 measures pulses after the trip. */
}

bool tfw_pulse2_busy(uint8_t axis)
{
    if (axis >= MAX_AXES) return false;
    axis_t *a = &s_ax[axis];
    if (!a->ok || !a->active) return false;
    if (!a->done) return true;
    /* finalise: output is already forced low by the watch-point ISR */
    mcpwm_timer_start_stop(a->timer, MCPWM_TIMER_STOP_EMPTY);
    pcnt_unit_stop(a->pcnt);
    gpio_set_level(a->en_gpio, 0);
    a->active = false;
    return false;
}

uint32_t IRAM_ATTR tfw_pulse2_count_isr(uint8_t axis)
{
    if (axis >= MAX_AXES || !s_ax[axis].ok) return 0;
    return axis_total(&s_ax[axis]);
}

uint32_t tfw_pulse2_count(uint8_t axis)
{
    if (axis >= MAX_AXES || !s_ax[axis].ok) return 0;
    axis_t *a = &s_ax[axis];
    int n = 0;
    uint32_t c1, c2;
    do { c1 = a->completed; pcnt_unit_get_count(a->pcnt, &n); c2 = a->completed; } while (c1 != c2);
    return tfw_total_count(c1, n);                /* no monotonic guard here: report what PCNT says */
}

void tfw_pulse2_stats(uint8_t axis, tfw_pulse_stats_t *out)
{
    memset(out, 0, sizeof(*out));
    out->max_period_ticks = s_max_ticks;
    out->upd_max_us = s_upd.max_us;
    out->upd_late = s_upd.late;
    if (axis < MAX_AXES) {
        out->isr_lat_max_ns = s_ax[axis].isr_lat_ns_max;
        out->freq_actual = s_ax[axis].freq_actual;
        out->last_ticks = s_ax[axis].cur_ticks;
    }
}

void tfw_pulse2_clear_stats(void)
{
    s_upd.max_us = 0; s_upd.late = 0; s_upd.calls = 0;
    for (int i = 0; i < MAX_AXES; ++i) s_ax[i].isr_lat_ns_max = 0;
}

/* ------------------------------------------------------------------ resource probe (TFW-02 sec.1) */
void tfw_pulse2_probe_caps(tfw_caps_t *out)
{
    memset(out, 0, sizeof(*out));
    const int pin = CONFIG_TFW_PROBE_GPIO;

    /* group 1 timer */
    mcpwm_timer_handle_t t1 = NULL;
    mcpwm_timer_config_t tc = {.group_id = 1, .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT, .resolution_hz = 10000000,
                               .count_mode = MCPWM_TIMER_COUNT_MODE_UP, .period_ticks = 1000};
    esp_err_t e = mcpwm_new_timer(&tc, &t1);
    out->err_group1 = e; out->group1_ok = (e == ESP_OK);
    if (t1) mcpwm_del_timer(t1);

    /* max period ticks accepted by mcpwm_new_timer (group 0) */
    static const uint32_t cand[] = {1000, 32767, 65535, 65536, 131071, 262143, 1048575};
    for (unsigned i = 0; i < sizeof(cand) / sizeof(cand[0]); ++i) {
        mcpwm_timer_handle_t t = NULL;
        mcpwm_timer_config_t c = {.group_id = 0, .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT, .resolution_hz = 10000000,
                                  .count_mode = MCPWM_TIMER_COUNT_MODE_UP, .period_ticks = cand[i]};
        if (mcpwm_new_timer(&c, &t) == ESP_OK) { out->max_period_ticks = cand[i]; mcpwm_del_timer(t); }
        else break;
    }

    if (out->max_period_ticks > 0 && out->max_period_ticks < s_max_ticks) s_max_ticks = out->max_period_ticks;   /* never raised above the assumed 65535 */

    /* second generator on one operator (group 0) */
    {
        mcpwm_timer_handle_t t = NULL; mcpwm_oper_handle_t o = NULL; mcpwm_gen_handle_t g1 = NULL, g2 = NULL;
        mcpwm_timer_config_t c = {.group_id = 0, .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT, .resolution_hz = 10000000,
                                  .count_mode = MCPWM_TIMER_COUNT_MODE_UP, .period_ticks = 1000};
        mcpwm_operator_config_t oc = {.group_id = 0};
        e = mcpwm_new_timer(&c, &t);
        if (e == ESP_OK) e = mcpwm_new_operator(&oc, &o);
        if (e == ESP_OK) { mcpwm_generator_config_t g = {.gen_gpio_num = pin}; e = mcpwm_new_generator(o, &g, &g1); }
        if (e == ESP_OK) { mcpwm_generator_config_t g = {.gen_gpio_num = -1}; e = mcpwm_new_generator(o, &g, &g2); }
        out->err_gen2 = e; out->gen2_ok = (e == ESP_OK);
        if (g2) mcpwm_del_generator(g2);
        if (g1) mcpwm_del_generator(g1);
        if (o) mcpwm_del_operator(o);
        if (t) mcpwm_del_timer(t);
    }

    /* two channels on one PCNT unit */
    {
        pcnt_unit_handle_t u = NULL; pcnt_channel_handle_t c1 = NULL, c2 = NULL;
        pcnt_unit_config_t uc = {.low_limit = -1, .high_limit = 30000};
        e = pcnt_new_unit(&uc, &u);
        if (e == ESP_OK) { pcnt_chan_config_t cc = {.edge_gpio_num = pin, .level_gpio_num = -1}; e = pcnt_new_channel(u, &cc, &c1); }
        if (e == ESP_OK) { pcnt_chan_config_t cc = {.edge_gpio_num = -1, .level_gpio_num = pin}; e = pcnt_new_channel(u, &cc, &c2); }
        out->err_pcnt_ch2 = e; out->pcnt_ch2_ok = (e == ESP_OK);
        if (c2) pcnt_del_channel(c2);
        if (c1) pcnt_del_channel(c1);
        if (u) pcnt_del_unit(u);
    }

    /* ETM PCNT -> MCPWM: optional experiment, not implemented in this version */
    out->etm_tested = false;
    out->etm_pcnt_ok = false;
    out->err_etm = ESP_ERR_NOT_SUPPORTED;

    ESP_LOGI(TAG, "CAPS group1_ok=%d (err 0x%x) gen2_ok=%d (err 0x%x) pcnt_ch2_ok=%d (err 0x%x) max_period_ticks=%lu etm=NOT_TESTED",
             out->group1_ok, (unsigned)out->err_group1, out->gen2_ok, (unsigned)out->err_gen2,
             out->pcnt_ch2_ok, (unsigned)out->err_pcnt_ch2, (unsigned long)out->max_period_ticks);
}
