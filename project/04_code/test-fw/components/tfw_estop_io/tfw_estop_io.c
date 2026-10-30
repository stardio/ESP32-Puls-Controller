/* TFW-04. New code (the existing servo1axis has no latched e-stop, STOP is a 100 ms debounced ladder input on
 * GPIO18). NOT verified on hardware. Open API questions (see notes): one GPIO as fault input of two MCPWM
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

static tfw_es_t s_es;
static int s_gpio = -1;
static bool s_active_low = true;
static mcpwm_fault_handle_t s_fault[2];
static mcpwm_oper_handle_t s_oper[2];
static bool s_bound[2];
static uint16_t s_bind_ok, s_boot_level, s_last_err;
static volatile bool s_pending;
static volatile uint32_t s_trip_cyc;      /* CPU cycle counter at trip: esp_timer is not used in the ISR (M-01) */
static uint32_t s_cpu_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
static uint32_t s_pulses_after[2];
/* N-02: fail-safe. Motion stays refused until tfw_estop_init() has completed successfully (never initialised, any
 * init step failed, or the watch task could not be created). Reported in es_last_err / es_bind_ok. */
static volatile bool s_init_ok;

#if CONFIG_TFW_DEBUG_GPIO_ESTOP >= 0
#define DBG_ESTOP() gpio_set_level(CONFIG_TFW_DEBUG_GPIO_ESTOP, 1), gpio_set_level(CONFIG_TFW_DEBUG_GPIO_ESTOP, 0)
#else
#define DBG_ESTOP() ((void)0)
#endif

void tfw_estop_set_cpu_mhz(uint32_t mhz) { if (mhz) s_cpu_mhz = mhz; }

bool IRAM_ATTR tfw_estop_input_active(void)
{
    if (s_gpio < 0) return false;
    return gpio_get_level((gpio_num_t)s_gpio) == (s_active_low ? 0 : 1);
}

static void IRAM_ATTR es_isr(void *arg)
{
    (void)arg;
    DBG_ESTOP();
    const uint32_t c0 = tfw_pulse2_count_isr(0), c1 = tfw_pulse2_count_isr(1);   /* snapshot first (design pseudo-code) */
    /* N-03: latch BEFORE the stop so that a task that is starting an axis at this moment sees the latch when it
     * re-checks after its timer start (tfw_pulse2 start_common) - whichever order the two run in, the axis ends stopped. */
    if (!s_es.latched) {
        tfw_es_trip(&s_es, c0, c1);                /* always_inline: part of this IRAM function (M-01) */
        s_trip_cyc = esp_cpu_get_cycle_count();
        s_pending = true;
    }
    if (s_es.path & 1u) {                      /* path A: software stop */
        tfw_pulse2_stop(0);
        tfw_pulse2_stop(1);
    }
}

static void es_task(void *arg)
{
    (void)arg;
    for (;;) {
        tfw_es_input_sample(&s_es, tfw_estop_input_active(), 1);
        if (s_pending && (uint32_t)((esp_cpu_get_cycle_count() - s_trip_cyc) / s_cpu_mhz) >= 10000u) {
            for (uint8_t i = 0; i < 2; ++i) s_pulses_after[i] = tfw_es_pulses_after(s_es.snap[i], tfw_pulse2_count(i));
            tfw_pulse2_stop(0);                /* clean-up for path B-only: output was braked, timers still run */
            tfw_pulse2_stop(1);
            s_pending = false;
            ESP_LOGW(TAG, "TRIP #%u pulses after trip: ax1=%lu ax2=%lu path=%u", (unsigned)s_es.trip_count,
                     (unsigned long)s_pulses_after[0], (unsigned long)s_pulses_after[1], (unsigned)s_es.path);
        }
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
    s_es.path = sel;
    for (uint8_t g = 0; g < 2; ++g) {
        if (!s_bound[g]) continue;
        esp_err_t e = set_brake_action(g, (sel & 2u) != 0);
        if (e != ESP_OK) { s_last_err = (uint16_t)e; ESP_LOGE(TAG, "brake action group %u: 0x%x", (unsigned)g, (unsigned)e); }
    }
}

static esp_err_t estop_init_body(int gpio, bool active_low, mcpwm_oper_handle_t oper[2])
{
    s_gpio = gpio; s_active_low = active_low;
    tfw_es_init(&s_es);
    s_oper[0] = oper ? oper[0] : NULL;
    s_oper[1] = oper ? oper[1] : NULL;

    /* path B first: mcpwm_new_gpio_fault configures the pin; gpio_config below must come after it */
    for (uint8_t g = 0; g < 2; ++g) {
        if (!s_oper[g]) continue;
        mcpwm_gpio_fault_config_t fc = {
            .group_id = g, .gpio_num = gpio,
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

    gpio_config_t in = {
        .pin_bit_mask = 1ULL << gpio, .mode = GPIO_MODE_INPUT,
        .pull_up_en = active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = active_low ? GPIO_INTR_NEGEDGE : GPIO_INTR_POSEDGE,
    };
    esp_err_t e = gpio_config(&in);
    if (e != ESP_OK) { s_last_err = (uint16_t)e; return e; }
    /* M-01: the IRAM flag is used only in the IRAM-safe build. The iram_off build installs a normal ISR so that
     * the comparison of T-06 does not run an IRAM-flagged ISR that calls flash-resident driver code.
     * M-03: the e-stop ISR gets the highest level of the three ISRs (Kconfig TFW_IRQ_PRIO_*). */
    int flags = (CONFIG_TFW_IRQ_PRIO_ESTOP == 3) ? ESP_INTR_FLAG_LEVEL3 : (CONFIG_TFW_IRQ_PRIO_ESTOP == 2) ? ESP_INTR_FLAG_LEVEL2 :
            (CONFIG_TFW_IRQ_PRIO_ESTOP == 1) ? ESP_INTR_FLAG_LEVEL1 : 0;
#if CONFIG_PCNT_ISR_IRAM_SAFE
    flags |= ESP_INTR_FLAG_IRAM;
#endif
    e = gpio_install_isr_service(flags);
    if (e == ESP_ERR_INVALID_STATE) {
        /* N-02: the precondition of M-03 (level, IRAM flag) is not met: exported in es_last_err, T-07 becomes NOT_RUN */
        s_last_err = (uint16_t)ESP_ERR_INVALID_STATE;
        ESP_LOGW(TAG, "GPIO ISR service already installed: requested level/IRAM flags NOT applied");
    }
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) { s_last_err = (uint16_t)e; return e; }
    e = gpio_isr_handler_add((gpio_num_t)gpio, es_isr, NULL);
    if (e != ESP_OK) { s_last_err = (uint16_t)e; return e; }
#if CONFIG_TFW_DEBUG_GPIO_ESTOP >= 0
    gpio_config_t dbg = {.pin_bit_mask = 1ULL << CONFIG_TFW_DEBUG_GPIO_ESTOP, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&dbg);
#endif
    s_boot_level = (uint16_t)gpio_get_level((gpio_num_t)gpio);
    /* M-04: an input that is already active at boot latches immediately (no edge will ever come) */
    tfw_es_boot_check(&s_es, tfw_estop_input_active());
    if (s_es.latched) ESP_LOGW(TAG, "e-stop input ACTIVE at boot: LATCHED, RUN commands are refused until release");
    ESP_LOGI(TAG, "e-stop ISR on core %d, level %d, IRAM flag %d", (int)esp_cpu_get_core_id(), CONFIG_TFW_IRQ_PRIO_ESTOP,
             TFW_B_PCNT_ISR_IRAM_SAFE);
    ESP_LOGI(TAG, "GPIO%d level at boot = %u (%s, active %s). bind_ok=%u (0 none,1 g0,2 g1,3 both). "
             "Existing servo1axis code used GPIO18: check the wiring.",
             gpio, (unsigned)s_boot_level, tfw_estop_input_active() ? "ACTIVE (already tripped?)" : "inactive",
             active_low ? "LOW" : "HIGH", (unsigned)s_bind_ok);
    tfw_estop_set_path(3);
    if (xTaskCreate(es_task, "tfw_es", 3072, NULL, 9, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

esp_err_t tfw_estop_init(int gpio, bool active_low, mcpwm_oper_handle_t oper[2])
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

uint16_t tfw_estop_release(void)
{
    uint16_t r = tfw_es_release(&s_es, tfw_estop_input_active());
    if (r != 0) return r;
    for (uint8_t g = 0; g < 2; ++g) {
        if (!s_bound[g]) continue;
        esp_err_t e = mcpwm_operator_recover_from_fault(s_oper[g], s_fault[g]);
        if (e != ESP_OK) { s_last_err = (uint16_t)e; ESP_LOGW(TAG, "recover group %u: 0x%x", (unsigned)g, (unsigned)e); }
    }
    s_pending = false;
    return 0;                                   /* no automatic restart */
}

bool tfw_estop_latched(void) { return s_es.latched != 0; }
/* true while latched OR while the input is still active (M-04): the condition to refuse motion */
bool tfw_estop_blocking(void) { return !s_init_ok || tfw_es_blocking(&s_es, tfw_estop_input_active()); }

void tfw_estop_stats(tfw_es_stats_t *out)
{
    memset(out, 0, sizeof(*out));
    out->state = tfw_es_state(&s_es, tfw_estop_input_active());
    out->trip_count = s_es.trip_count;
    out->bind_ok = s_bind_ok;
    out->boot_level = s_boot_level;
    out->last_err = s_last_err;
    out->isr_lat_ns = 0xFFFFFFFFu;
    out->pulses_after[0] = s_pulses_after[0];
    out->pulses_after[1] = s_pulses_after[1];
}
