/* TFW-04. New code (the existing servo1axis has no latched e-stop, STOP is a 100 ms debounced ladder input on
 * GPIO18). Baseline v0.4: STOP is PER AXIS (axis 1 GPIO19 = DI2, axis 2 GPIO17 = DI4, NC contact), and each axis has a
 * START edge input (GPIO54 / GPIO18). A STOP trip forces only its own axis LOW; the other axis and the shared pulse
 * output enable (GPIO33) are not touched. NOT verified on hardware. Open API questions (see notes): one GPIO as fault input of two MCPWM
 * groups; exact brake API behaviour. Failures are logged and reported in es_fault_bind_ok / es_last_err. */
#include "tfw_estop_io.h"
#include <string.h>
#include "sdkconfig.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_cpu.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tfw_logic.h"
#include "tfw_cfg.h"
#include "tfw_pulse2.h"

static const char *TAG = "TFW_ESTOP";

#define NAX 2
static tfw_es_t s_es[NAX];
static tfw_start_t s_st[NAX];
static int s_gpio[NAX] = {-1, -1};
static int s_start_gpio[NAX] = {-1, -1};
static bool s_active_low = true;
static bool s_start_active_low = true;
static void (*s_start_cb)(uint8_t axis);
static mcpwm_fault_handle_t s_fault[NAX];
static mcpwm_oper_handle_t s_oper[NAX];
static bool s_bound[NAX];
static uint16_t s_bind_ok, s_boot_level, s_last_err;
static volatile bool s_pending[NAX];
static volatile uint32_t s_trip_cyc[NAX];      /* CPU cycle counter at trip: esp_timer is not used in the ISR (M-01) */
static uint32_t s_cpu_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
static uint32_t s_pulses_after[NAX];
static uint16_t s_start_edges[NAX], s_start_ignored[NAX];
/* N-02: fail-safe. Motion stays refused until tfw_estop_init() has completed successfully (never initialised, any
 * init step failed, or the watch task could not be created). Reported in es_last_err / es_bind_ok. */
static volatile bool s_init_ok;

#if CONFIG_TFW_DEBUG_GPIO_ESTOP >= 0
#define DBG_ESTOP() gpio_set_level(CONFIG_TFW_DEBUG_GPIO_ESTOP, 1), gpio_set_level(CONFIG_TFW_DEBUG_GPIO_ESTOP, 0)
#else
#define DBG_ESTOP() ((void)0)
#endif

void tfw_estop_set_cpu_mhz(uint32_t mhz) { if (mhz) s_cpu_mhz = mhz; }

bool IRAM_ATTR tfw_estop_input_active(uint8_t axis)
{
    if (axis >= NAX || s_gpio[axis] < 0) return false;
    return gpio_get_level((gpio_num_t)s_gpio[axis]) == (s_active_low ? 0 : 1);
}

static void IRAM_ATTR es_isr(void *arg)
{
    const uint8_t ax = (uint8_t)(uintptr_t)arg;     /* the axis whose STOP input fired */
    DBG_ESTOP();
    const uint32_t c0 = tfw_pulse2_count_isr(0), c1 = tfw_pulse2_count_isr(1);   /* snapshot first (design pseudo-code) */
    /* N-03: latch BEFORE the stop so that a task that is starting an axis at this moment sees the latch when it
     * re-checks after its timer start (tfw_pulse2 start_common) - whichever order the two run in, the axis ends stopped. */
    if (!s_es[ax].latched) {
        tfw_es_trip(&s_es[ax], c0, c1);            /* always_inline: part of this IRAM function (M-01) */
        s_trip_cyc[ax] = esp_cpu_get_cycle_count();
        s_pending[ax] = true;
    }
    if (s_es[ax].path & 1u) {                  /* path A: software stop of THIS axis only (baseline v0.4 sec.3) */
        tfw_pulse2_stop(ax);
    }
}

bool tfw_estop_blocking_axis(uint8_t axis)
{
    if (!s_init_ok) return true;
    if (axis >= NAX || s_gpio[axis] < 0) return false;
    return tfw_es_blocking(&s_es[axis], tfw_estop_input_active(axis));
}

static void start_poll(uint8_t ax)
{
    if (s_start_gpio[ax] < 0) return;
    const bool raw = gpio_get_level((gpio_num_t)s_start_gpio[ax]) == (s_start_active_low ? 0 : 1);
    const bool input = tfw_estop_input_active(ax);
    /* STOP input of this axis active -> the START edge is consumed and ignored (edge, not level: holding START through
     * the STOP does not restart after release) */
    if (!tfw_start_sample(&s_st[ax], raw, input, 1, CONFIG_TFW_START_DEBOUNCE_MS)) {
        return;
    }
    s_start_edges[ax]++;
    /* STOP input released but still latched: this START edge re-arms the axis (baseline v0.4 sec.3: restart only by a
     * START edge after release). tfw_es_release() refuses until the input has been inactive 10 ms. */
    if (tfw_estop_latched_axis(ax) && tfw_estop_release_axis(ax) != 0) { s_start_ignored[ax]++; return; }
    if (s_start_cb) s_start_cb(ax);
}

/* One 1 ms pass over both axes (also called directly by the host test). */
void tfw_estop_poll_once(void)
{
    for (uint8_t ax = 0; ax < NAX; ++ax) {
        if (s_gpio[ax] < 0) continue;
        tfw_es_input_sample(&s_es[ax], tfw_estop_input_active(ax), 1);
        start_poll(ax);
        if (s_pending[ax] && (uint32_t)((esp_cpu_get_cycle_count() - s_trip_cyc[ax]) / s_cpu_mhz) >= 10000u) {
            s_pulses_after[ax] = tfw_es_pulses_after(s_es[ax].snap[ax], tfw_pulse2_count(ax));
            tfw_pulse2_stop(ax);           /* clean-up for path B-only: output was braked, timer still runs. This axis only */
            s_pending[ax] = false;
            ESP_LOGW(TAG, "TRIP #%u axis %u pulses after trip: %lu path=%u", (unsigned)s_es[ax].trip_count,
                     (unsigned)(ax + 1), (unsigned long)s_pulses_after[ax], (unsigned)s_es[ax].path);
        }
    }
}

static void es_task(void *arg)
{
    (void)arg;
    for (;;) {
        tfw_estop_poll_once();
        vTaskDelay(pdMS_TO_TICKS(1));          /* needs CONFIG_FREERTOS_HZ=1000 */
    }
}

static esp_err_t set_brake_action(uint8_t g, bool on)
{
    mcpwm_gen_handle_t gen = tfw_pulse2_generator(g);
    if (!s_bound[g] || !gen) return ESP_ERR_INVALID_STATE;
    return mcpwm_generator_set_action_on_brake_event(gen,
        MCPWM_GEN_BRAKE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_OPER_BRAKE_MODE_OST,
                                     on ? MCPWM_GEN_ACTION_LOW : MCPWM_GEN_ACTION_KEEP));
}

void tfw_estop_set_path(uint8_t sel)
{
    if (!tfw_es_path_valid(sel)) return;
    for (uint8_t g = 0; g < NAX; ++g) {
        s_es[g].path = sel;
        if (!s_bound[g]) continue;
        esp_err_t e = set_brake_action(g, (sel & 2u) != 0);
        if (e != ESP_OK) { s_last_err = (uint16_t)e; ESP_LOGE(TAG, "brake action group %u: 0x%x", (unsigned)g, (unsigned)e); }
    }
}

void tfw_estop_set_start_cb(void (*cb)(uint8_t axis)) { s_start_cb = cb; }

/* One STOP input per axis; MCPWM group g belongs to axis g, so the path B fault input of group g is the STOP pin of axis g. */
static esp_err_t estop_init_body(const int gpio[NAX], bool active_low, mcpwm_oper_handle_t oper[NAX])
{
    s_active_low = active_low;
    s_start_active_low = TFW_B_START_ACTIVE_LOW;
    s_start_gpio[0] = CONFIG_TFW_START_GPIO; s_start_gpio[1] = TFW_B_AX2_IO_PRESENT ? CONFIG_TFW_START2_GPIO : -1;   /* D-068 */
    s_bind_ok = 0; s_boot_level = 0;
    for (uint8_t g = 0; g < NAX; ++g) {
        s_gpio[g] = gpio[g];
        tfw_es_init(&s_es[g]);
        s_bound[g] = false; s_pending[g] = false; s_pulses_after[g] = 0;
        s_start_edges[g] = s_start_ignored[g] = 0;
        s_oper[g] = oper ? oper[g] : NULL;
    }

    for (uint8_t g = 0; g < NAX; ++g) {
        if (s_gpio[g] < 0) continue;
        /* path B first: mcpwm_new_gpio_fault configures the pin; gpio_config below must come after it */
        if (s_oper[g]) {
            mcpwm_gpio_fault_config_t fc = {
                .group_id = g, .gpio_num = s_gpio[g],
                .flags = {.active_level = active_low ? 0 : 1, .pull_up = active_low, .pull_down = !active_low},
            };
            esp_err_t e = mcpwm_new_gpio_fault(&fc, &s_fault[g]);
            if (e == ESP_OK) {
                mcpwm_brake_config_t bc = {.fault = s_fault[g], .brake_mode = MCPWM_OPER_BRAKE_MODE_OST};
                e = mcpwm_operator_set_brake_on_fault(s_oper[g], &bc);
            }
            if (e == ESP_OK) {
                s_bound[g] = true;
                s_bind_ok |= (g == 0) ? 1u : 2u;
            } else {
                if (!s_last_err) s_last_err = (uint16_t)e;
                ESP_LOGE(TAG, "fault bind group %u FAILED: %s (0x%x)", (unsigned)g, esp_err_to_name(e), (unsigned)e);
            }
        }
    }

    /* M-01: the IRAM flag is used only in the IRAM-safe build. The iram_off build installs a normal ISR so that
     * the comparison of T-06 does not run an IRAM-flagged ISR that calls flash-resident driver code.
     * M-03: the e-stop ISR gets the highest level of the three ISRs (Kconfig TFW_IRQ_PRIO_*). */
    int flags = (CONFIG_TFW_IRQ_PRIO_ESTOP == 3) ? ESP_INTR_FLAG_LEVEL3 : (CONFIG_TFW_IRQ_PRIO_ESTOP == 2) ? ESP_INTR_FLAG_LEVEL2 :
            (CONFIG_TFW_IRQ_PRIO_ESTOP == 1) ? ESP_INTR_FLAG_LEVEL1 : 0;
#if CONFIG_PCNT_ISR_IRAM_SAFE
    flags |= ESP_INTR_FLAG_IRAM;
#endif
    bool service_checked = false;
    for (uint8_t g = 0; g < NAX; ++g) {
        if (s_gpio[g] < 0) continue;
        gpio_config_t in = {
            .pin_bit_mask = 1ULL << s_gpio[g], .mode = GPIO_MODE_INPUT,
            .pull_up_en = active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
            .pull_down_en = active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
            .intr_type = active_low ? GPIO_INTR_NEGEDGE : GPIO_INTR_POSEDGE,
        };
        esp_err_t e = gpio_config(&in);
        if (e != ESP_OK) { s_last_err = (uint16_t)e; return e; }
        if (!service_checked) {
            service_checked = true;
            e = gpio_install_isr_service(flags);
            if (e == ESP_ERR_INVALID_STATE) {
                /* N-02: the precondition of M-03 (level, IRAM flag) is not met: exported in es_last_err, T-07 becomes NOT_RUN */
                s_last_err = (uint16_t)ESP_ERR_INVALID_STATE;
                ESP_LOGW(TAG, "GPIO ISR service already installed: requested level/IRAM flags NOT applied");
            }
            if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) { s_last_err = (uint16_t)e; return e; }
        }
        e = gpio_isr_handler_add((gpio_num_t)s_gpio[g], es_isr, (void *)(uintptr_t)g);
        if (e != ESP_OK) { s_last_err = (uint16_t)e; return e; }
    }
    /* START inputs: polled (no ISR), so no interrupt type */
    for (uint8_t g = 0; g < NAX; ++g) {
        if (s_start_gpio[g] < 0) continue;
        gpio_config_t sin = {
            .pin_bit_mask = 1ULL << s_start_gpio[g], .mode = GPIO_MODE_INPUT,
            .pull_up_en = s_start_active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
            .pull_down_en = s_start_active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t e = gpio_config(&sin);
        if (e != ESP_OK) { s_last_err = (uint16_t)e; return e; }
        const bool act = gpio_get_level((gpio_num_t)s_start_gpio[g]) == (s_start_active_low ? 0 : 1);
        tfw_start_init(&s_st[g], act);          /* held at boot: not an edge */
    }
#if CONFIG_TFW_DEBUG_GPIO_ESTOP >= 0
    gpio_config_t dbg = {.pin_bit_mask = 1ULL << CONFIG_TFW_DEBUG_GPIO_ESTOP, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&dbg);
#endif
    for (uint8_t g = 0; g < NAX; ++g) {
        if (s_gpio[g] < 0) continue;
        if (gpio_get_level((gpio_num_t)s_gpio[g])) s_boot_level |= (uint16_t)(1u << g);
        /* M-04: an input that is already active at boot latches immediately (no edge will ever come) */
        tfw_es_boot_check(&s_es[g], tfw_estop_input_active(g));
        if (s_es[g].latched) ESP_LOGW(TAG, "axis %u STOP input ACTIVE at boot: LATCHED, RUN commands for it are refused until release", (unsigned)(g + 1));
    }
    ESP_LOGI(TAG, "e-stop ISR on core %d, level %d, IRAM flag %d", (int)esp_cpu_get_core_id(), CONFIG_TFW_IRQ_PRIO_ESTOP,
             TFW_B_PCNT_ISR_IRAM_SAFE);
    /* F-10: wording of the board signal names */
    ESP_LOGI(TAG, "STOP axis 1: GPIO%d = DI2(J15), level at boot = %u (%s, active %s)", s_gpio[0], (unsigned)(s_boot_level & 1u),
             tfw_estop_input_active(0) ? "ACTIVE (already tripped?)" : "inactive", active_low ? "LOW" : "HIGH");
    ESP_LOGI(TAG, "STOP axis 2: GPIO%d = DI4, level at boot = %u (%s, active %s). bind_ok=%u (0 none,1 g0,2 g1,3 both). "
             "START (edge, ignored while STOP): axis 1 GPIO%d = DI1, axis 2 GPIO%d = DI3, active %s.",
             s_gpio[1], (unsigned)((s_boot_level >> 1) & 1u),
             tfw_estop_input_active(1) ? "ACTIVE (already tripped?)" : "inactive", active_low ? "LOW" : "HIGH",
             (unsigned)s_bind_ok, s_start_gpio[0], s_start_gpio[1], s_start_active_low ? "LOW" : "HIGH");
    tfw_estop_set_path(3);
    if (xTaskCreate(es_task, "tfw_es", 3072, NULL, 9, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

esp_err_t tfw_estop_init(const int gpio[2], bool active_low, mcpwm_oper_handle_t oper[2])
{
    s_init_ok = false;
    esp_err_t e = estop_init_body(gpio, active_low, oper);
    if (e != ESP_OK) {
        if (!s_last_err) s_last_err = (uint16_t)e;
        ESP_LOGE(TAG, "e-stop init FAILED (0x%x): motion is REFUSED permanently (no ISR / no latch)", (unsigned)e);
        return e;
    }
    s_init_ok = true;
    return ESP_OK;
}

uint16_t tfw_estop_release_axis(uint8_t axis)
{
    if (axis >= NAX) return TFW_ST_REJECT_BUSY;
    uint16_t r = tfw_es_release(&s_es[axis], tfw_estop_input_active(axis));
    if (r != 0) return r;
    if (s_bound[axis]) {
        esp_err_t e = mcpwm_operator_recover_from_fault(s_oper[axis], s_fault[axis]);
        if (e != ESP_OK) { s_last_err = (uint16_t)e; ESP_LOGW(TAG, "recover group %u: 0x%x", (unsigned)axis, (unsigned)e); }
    }
    s_pending[axis] = false;
    return 0;                                   /* no automatic restart */
}

/* Releases every axis independently; returns the first refusal (0 = all released / nothing latched). */
uint16_t tfw_estop_release(void)
{
    uint16_t first = 0;
    for (uint8_t a = 0; a < NAX; ++a) {
        if (s_gpio[a] < 0 || !s_es[a].latched) continue;
        uint16_t r = tfw_estop_release_axis(a);
        if (r && !first) first = r;
    }
    return first;
}

bool tfw_estop_latched_axis(uint8_t axis) { return axis < NAX && s_es[axis].latched != 0; }
bool tfw_estop_latched(void) { return s_es[0].latched != 0 || s_es[1].latched != 0; }
/* true while latched OR while the input is still active (M-04), per axis: the condition to refuse motion of that axis */
bool tfw_estop_blocking(void) { return tfw_estop_blocking_axis(0) || tfw_estop_blocking_axis(1); }

uint16_t tfw_estop_start_edges(uint8_t axis) { return axis < NAX ? s_start_edges[axis] : 0; }
uint16_t tfw_estop_start_ignored(uint8_t axis) { return axis < NAX ? s_start_ignored[axis] : 0; }

void tfw_estop_stats(tfw_es_stats_t *out)
{
    memset(out, 0, sizeof(*out));
    /* state / trip_count are the worst / sum over both axes; the register map is unchanged (per-axis pulses_after exist) */
    uint16_t st = 0;
    for (uint8_t a = 0; a < NAX; ++a) {
        uint16_t x = (s_gpio[a] < 0) ? 0 : tfw_es_state(&s_es[a], tfw_estop_input_active(a));
        if (x > st) st = x;
        out->trip_count = (uint16_t)(out->trip_count + s_es[a].trip_count);
        out->pulses_after[a] = s_pulses_after[a];
    }
    out->state = st;
    out->bind_ok = s_bind_ok;
    out->boot_level = s_boot_level;      /* bit0 = axis 1 STOP pin level at init, bit1 = axis 2 */
    out->last_err = s_last_err;
    out->isr_lat_ns = 0xFFFFFFFFu;
}
