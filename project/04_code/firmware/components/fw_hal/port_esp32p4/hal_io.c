/* fw_hal digital I/O for ESP32-P4: shared pulse output enable (GPIO33), servo/general outputs, per-axis STOP
 * (NC contact; GPIO ISR = path A, MCPWM GPIO fault brake = path B), START edge polling, spare DI.
 * Source: project/04_code/test-fw/components/tfw_estop_io/tfw_estop_io.c (reviewed: M-01 ISR in IRAM, M-03 ISR level,
 * M-04 STOP active at boot latches, N-02 init failure refuses motion, N-03 latch before stop). Differences: pins from the
 * product Kconfig (X: STOP 19 / START 54, Y: STOP 8 / START 7), no statistics / debug / path selection (both paths
 * always on), release only by an explicit start (D-072: START edge or HMI/Modbus command), spare DI 18/17/15/16 read-only.
 * NOT verified on hardware. Open API question carried over from test-fw: exact behaviour of
 * mcpwm_operator_set_brake_on_fault / recover_from_fault (IDF 5.5.3 headers not available here). */
#include <string.h>
#include "sdkconfig.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fw_cfg.h"
#include "fw_hal.h"
#include "fw_stop.h"
#include "port_internal.h"

static const char *TAG = "FW_IO";
#define NAX FW_MAX_AXES_HAL

static fw_es_t s_es[NAX];
static fw_start_t s_st[NAX];
static int s_stop_gpio[NAX] = {-1, -1};
static int s_start_gpio[NAX] = {-1, -1};
static int s_spare_gpio[4] = {-1, -1, -1, -1};       /* DI3, DI4 (X), DI7, DI8 (Y) */
static mcpwm_fault_handle_t s_fault[NAX];
static bool s_bound[NAX];
static volatile bool s_init_ok;                      /* N-02: motion refused until init completed */
static void (*s_start_cb)(uint8_t axis);
static uint16_t s_start_edges[NAX], s_start_ignored[NAX];
static uint8_t s_axis_count;
static bool s_oe_allowed;
static uint16_t s_do_state, s_user_do;
static portMUX_TYPE s_stop_mux = portMUX_INITIALIZER_UNLOCKED;   /* latch set (ISR / level guard) vs latch release (task) */

/* ------------------------------------------------------------------ output enable GPIO33 */
static inline int oe_level(bool allow) { return (allow != (FW_B_OE_ACTIVE_LOW != 0)) ? 1 : 0; }

void fw_hal_oe_early_forbid(void)
{
    gpio_config_t oe = {.pin_bit_mask = 1ULL << CONFIG_FW_OE_GPIO, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&oe);
    gpio_set_level(CONFIG_FW_OE_GPIO, oe_level(false));
    s_oe_allowed = false;
}
void fw_hal_oe_forbid(void) { gpio_set_level(CONFIG_FW_OE_GPIO, oe_level(false)); s_oe_allowed = false; }
void fw_hal_oe_allow(void)  { gpio_set_level(CONFIG_FW_OE_GPIO, oe_level(true)); s_oe_allowed = true; }
bool fw_hal_oe_allowed(void) { return s_oe_allowed; }

/* ------------------------------------------------------------------ servo ON / general outputs */
static void do_write(int gpio, unsigned bit, bool on)
{
    gpio_set_level(gpio, on ? 1 : 0);
    s_do_state = (uint16_t)(on ? (s_do_state | (1u << bit)) : (s_do_state & ~(1u << bit)));
}
void fw_hal_servo_set(uint8_t axis, bool on)
{
    if (axis == 0) do_write(CONFIG_FW_DO1_GPIO, 0, on);
    else if (axis == 1 && s_axis_count >= 2) do_write(CONFIG_FW_DO2_GPIO, 1, on);
}
void fw_hal_user_do_set(uint16_t mask)
{
    s_user_do = mask;
    do_write(CONFIG_FW_DO3_GPIO, 2, (mask & 1u) != 0);
    do_write(CONFIG_FW_DO4_GPIO, 3, (mask & 2u) != 0);
}
uint16_t fw_hal_do_state(void) { return s_do_state; }

/* ------------------------------------------------------------------ STOP */
static inline bool IRAM_ATTR level_active(int gpio, bool active_low)
{
    return gpio_get_level((gpio_num_t)gpio) == (active_low ? 0 : 1);
}
bool IRAM_ATTR fw_hal_stop_input(uint8_t axis)
{
    if (axis >= NAX || s_stop_gpio[axis] < 0) return false;
    return level_active(s_stop_gpio[axis], FW_B_STOP_ACTIVE_LOW != 0);
}
bool fw_hal_stop_latched(uint8_t axis) { return axis < NAX && s_es[axis].latched != 0; }
bool fw_port_stop_blocking(uint8_t axis)
{
    if (!s_init_ok) return true;
    if (axis >= NAX || s_stop_gpio[axis] < 0) return axis < s_axis_count;       /* an existing axis without STOP input never runs */
    return fw_es_blocking(&s_es[axis], fw_hal_stop_input(axis));
}
bool fw_hal_stop_blocking(uint8_t axis) { return fw_port_stop_blocking(axis); }
bool fw_hal_io_ready(void) { return s_init_ok; }
uint16_t fw_hal_stop_state(uint8_t axis)
{
    if (axis >= NAX || s_stop_gpio[axis] < 0) return 0;
    return fw_es_state(&s_es[axis], fw_hal_stop_input(axis));
}
uint16_t fw_hal_stop_trips(uint8_t axis) { return axis < NAX ? s_es[axis].trip_count : 0; }
void fw_hal_set_start_cb(void (*cb)(uint8_t axis)) { s_start_cb = cb; }

/* ISR (IRAM): the latch is set BEFORE the stop so that a task that starts the axis at this moment sees it when it
 * re-checks after its timer start (N-03). fw_es_trip is always_inline: compiled into this IRAM function (M-01).
 * This axis only: the other axis and GPIO33 are not touched (baseline v0.4 sec.3). */
static void IRAM_ATTR stop_isr(void *arg)
{
    const uint8_t ax = (uint8_t)(uintptr_t)arg;
    portENTER_CRITICAL_ISR(&s_stop_mux);
    if (!s_es[ax].latched) fw_es_trip(&s_es[ax], 0u);
    portEXIT_CRITICAL_ISR(&s_stop_mux);
    fw_hal_pulse_stop(ax);
}

bool fw_hal_fault_bound(uint8_t axis) { return axis < NAX && s_bound[axis]; }

/* false = the brake could not be cleared: the caller must keep/restore the latch (review M-06) */
static bool release_hw(uint8_t ax)
{
    if (!s_bound[ax]) return true;                      /* nothing to recover; an unbound axis is not allowed to run anyway */
    esp_err_t e = mcpwm_operator_recover_from_fault(fw_port_operator(ax), s_fault[ax]);
    if (e != ESP_OK) ESP_LOGW(TAG, "axis %u: recover from fault 0x%x: latch kept", (unsigned)(ax + 1), (unsigned)e);
    return e == ESP_OK;
}

static void relatch(uint8_t ax)
{
    portENTER_CRITICAL(&s_stop_mux);
    if (!s_es[ax].latched) fw_es_trip(&s_es[ax], 0u);
    portEXIT_CRITICAL(&s_stop_mux);
    fw_hal_pulse_stop(ax);
}

int fw_hal_stop_release(uint8_t ax)
{
    if (!s_init_ok || ax >= NAX || s_stop_gpio[ax] < 0) return FW_ES_REFUSED;
    portENTER_CRITICAL(&s_stop_mux);
    const uint16_t r = fw_es_release(&s_es[ax], fw_hal_stop_input(ax));
    portEXIT_CRITICAL(&s_stop_mux);
    if (r != 0) return (int)r;
    if (!release_hw(ax)) { relatch(ax); return FW_ES_REFUSED; }
    return 0;
}

/* one 1 ms pass over the axes; also the place where a released latch is accepted */
static void poll_once(void)
{
    for (uint8_t ax = 0; ax < s_axis_count; ++ax) {
        if (s_stop_gpio[ax] < 0) continue;
        const bool was_latched = s_es[ax].latched != 0;
        const bool stop_in = fw_hal_stop_input(ax);
        const bool start_raw = s_start_gpio[ax] >= 0 && level_active(s_start_gpio[ax], FW_B_START_ACTIVE_LOW != 0);
        /* review M-02: level backup of the edge ISR. Input active but no latch -> latch and cut this axis now */
        portENTER_CRITICAL(&s_stop_mux);
        const bool tripped = fw_es_level_guard(&s_es[ax], stop_in);
        portEXIT_CRITICAL(&s_stop_mux);
        if (tripped) fw_hal_pulse_stop(ax);
        portENTER_CRITICAL(&s_stop_mux);                             /* same mutual exclusion as the ISR (M-01) */
        bool fire = fw_stopstart_step(&s_es[ax], &s_st[ax], stop_in, start_raw, CONFIG_FW_START_DEBOUNCE_MS,
                                      &s_start_edges[ax], &s_start_ignored[ax]);
        portEXIT_CRITICAL(&s_stop_mux);
        if (was_latched && !s_es[ax].latched && !release_hw(ax)) { relatch(ax); fire = false; }   /* START edge released the latch */
        if (s_es[ax].latched && fw_hal_pulse_active(ax)) fw_hal_pulse_stop(ax);   /* a latched axis never keeps running */
        if (fire && s_start_cb) s_start_cb(ax);
    }
}

static void io_task(void *arg)
{
    (void)arg;
    for (;;) {
        poll_once();
        vTaskDelay(pdMS_TO_TICKS(1));                 /* needs CONFIG_FREERTOS_HZ=1000 */
    }
}

static esp_err_t in_cfg(int gpio, bool pull_up, gpio_int_type_t intr)
{
    gpio_config_t c = {.pin_bit_mask = 1ULL << gpio, .mode = GPIO_MODE_INPUT,
                       .pull_up_en = pull_up ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
                       .pull_down_en = pull_up ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE, .intr_type = intr};
    return gpio_config(&c);
}

static esp_err_t io_init_body(uint8_t axis_count)
{
    const bool stop_low = FW_B_STOP_ACTIVE_LOW != 0, start_low = FW_B_START_ACTIVE_LOW != 0;
    s_axis_count = axis_count;
    s_stop_gpio[0] = CONFIG_FW_X_STOP_GPIO;  s_start_gpio[0] = CONFIG_FW_X_START_GPIO;
    s_stop_gpio[1] = axis_count >= 2 ? CONFIG_FW_Y_STOP_GPIO : -1;
    s_start_gpio[1] = axis_count >= 2 ? CONFIG_FW_Y_START_GPIO : -1;
    s_spare_gpio[0] = CONFIG_FW_X_DI3_GPIO; s_spare_gpio[1] = CONFIG_FW_X_DI4_GPIO;
    s_spare_gpio[2] = axis_count >= 2 ? CONFIG_FW_Y_DI7_GPIO : -1; s_spare_gpio[3] = axis_count >= 2 ? CONFIG_FW_Y_DI8_GPIO : -1;
    for (uint8_t g = 0; g < axis_count; ++g) {
        if (s_stop_gpio[g] < 0) { ESP_LOGE(TAG, "axis %u has no STOP input configured: refused", (unsigned)(g + 1)); return ESP_ERR_INVALID_ARG; }
        fw_es_init(&s_es[g]);
        s_bound[g] = false;
        s_start_edges[g] = s_start_ignored[g] = 0;
    }

    /* outputs: servo ON and DO3/DO4 start OFF */
    gpio_config_t outs = {.pin_bit_mask = (1ULL << CONFIG_FW_DO1_GPIO) | (1ULL << CONFIG_FW_DO2_GPIO) |
                                          (1ULL << CONFIG_FW_DO3_GPIO) | (1ULL << CONFIG_FW_DO4_GPIO), .mode = GPIO_MODE_OUTPUT};
    esp_err_t e = gpio_config(&outs);
    if (e != ESP_OK) return e;
    s_do_state = 0; do_write(CONFIG_FW_DO1_GPIO, 0, false); do_write(CONFIG_FW_DO2_GPIO, 1, false); fw_hal_user_do_set(0);

    /* path B first: mcpwm_new_gpio_fault configures the pin, gpio_config below must come after it */
    for (uint8_t g = 0; g < axis_count; ++g) {
        if (!fw_port_operator(g)) continue;
        mcpwm_gpio_fault_config_t fc = {
            .group_id = g, .gpio_num = s_stop_gpio[g],
            .flags = {.active_level = stop_low ? 0 : 1, .pull_up = stop_low, .pull_down = !stop_low},
        };
        e = mcpwm_new_gpio_fault(&fc, &s_fault[g]);
        if (e == ESP_OK) {
            mcpwm_brake_config_t bc = {.fault = s_fault[g], .brake_mode = MCPWM_OPER_BRAKE_MODE_OST};
            e = mcpwm_operator_set_brake_on_fault(fw_port_operator(g), &bc);
        }
        if (e == ESP_OK)
            e = mcpwm_generator_set_action_on_brake_event(fw_port_generator(g),
                    MCPWM_GEN_BRAKE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_OPER_BRAKE_MODE_OST, MCPWM_GEN_ACTION_LOW));
        if (e == ESP_OK) s_bound[g] = true;
        else ESP_LOGE(TAG, "axis %u: MCPWM fault path FAILED (%s): axis operation PROHIBITED (boot_status fault bit stays 0)", (unsigned)(g + 1), esp_err_to_name(e));
    }

    /* STOP ISR: highest level of the three ISRs (M-03); always in IRAM (review m-05: independent of the PCNT option) */
    int flags = (CONFIG_FW_IRQ_PRIO_STOP == 3) ? ESP_INTR_FLAG_LEVEL3 : (CONFIG_FW_IRQ_PRIO_STOP == 2) ? ESP_INTR_FLAG_LEVEL2 :
                (CONFIG_FW_IRQ_PRIO_STOP == 1) ? ESP_INTR_FLAG_LEVEL1 : 0;
    flags |= ESP_INTR_FLAG_IRAM;
    bool service_checked = false;
    for (uint8_t g = 0; g < axis_count; ++g) {
        e = in_cfg(s_stop_gpio[g], stop_low, stop_low ? GPIO_INTR_NEGEDGE : GPIO_INTR_POSEDGE);
        if (e != ESP_OK) return e;
        if (!service_checked) {
            service_checked = true;
            e = gpio_install_isr_service(flags);
            if (e == ESP_ERR_INVALID_STATE) ESP_LOGW(TAG, "GPIO ISR service already installed: requested level/IRAM flags NOT applied");
            else if (e != ESP_OK) return e;
        }
        e = gpio_isr_handler_add((gpio_num_t)s_stop_gpio[g], stop_isr, (void *)(uintptr_t)g);
        if (e != ESP_OK) return e;
    }
    /* START and spare DI: polled, no interrupt. Pull towards the inactive level so that an unconnected input is "no input". */
    for (uint8_t g = 0; g < axis_count; ++g) {
        if (s_start_gpio[g] < 0) { fw_start_init(&s_st[g], false); continue; }    /* Kconfig -1: no START input on this unit */
        e = in_cfg(s_start_gpio[g], start_low, GPIO_INTR_DISABLE);
        if (e != ESP_OK) return e;
        fw_start_init(&s_st[g], level_active(s_start_gpio[g], start_low));       /* held at boot: not an edge */
    }
    for (unsigned i = 0; i < 4; ++i)
        if (s_spare_gpio[i] >= 0) { e = in_cfg(s_spare_gpio[i], start_low, GPIO_INTR_DISABLE); if (e != ESP_OK) return e; }

    /* M-04: a STOP that is already active at boot latches at once */
    for (uint8_t g = 0; g < axis_count; ++g) {
        fw_es_boot_check(&s_es[g], fw_hal_stop_input(g));
        if (s_es[g].latched) ESP_LOGW(TAG, "axis %u STOP input ACTIVE at boot: latched until released by a START edge", (unsigned)(g + 1));
        ESP_LOGI(TAG, "axis %u: STOP GPIO%d (active %s, NC contact), START GPIO%d (edge, active %s; -1 = none), fault path %s", (unsigned)(g + 1),
                 s_stop_gpio[g], stop_low ? "LOW" : "HIGH", s_start_gpio[g], start_low ? "LOW" : "HIGH", s_bound[g] ? "bound" : "NOT bound");
    }
    if (xTaskCreate(io_task, "fw_io", 3072, NULL, 9, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

int fw_hal_io_init(uint8_t axis_count)
{
    s_init_ok = false;
    const esp_err_t e = io_init_body(axis_count);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "STOP/START init FAILED (0x%x): motion is REFUSED", (unsigned)e);
        return (int)e;
    }
    s_init_ok = true;
    return 0;
}

uint16_t fw_hal_di_raw(void)
{
    const bool start_low = FW_B_START_ACTIVE_LOW != 0;
    uint16_t v = 0;
    if (s_start_gpio[0] >= 0 && level_active(s_start_gpio[0], start_low)) v |= 1u << 0;
    if (fw_hal_stop_input(0)) v |= 1u << 1;
    if (s_spare_gpio[0] >= 0 && level_active(s_spare_gpio[0], start_low)) v |= 1u << 2;
    if (s_spare_gpio[1] >= 0 && level_active(s_spare_gpio[1], start_low)) v |= 1u << 3;
    if (s_start_gpio[1] >= 0 && level_active(s_start_gpio[1], start_low)) v |= 1u << 4;
    if (fw_hal_stop_input(1)) v |= 1u << 5;
    if (s_spare_gpio[2] >= 0 && level_active(s_spare_gpio[2], start_low)) v |= 1u << 6;
    if (s_spare_gpio[3] >= 0 && level_active(s_spare_gpio[3], start_low)) v |= 1u << 7;
    return v;
}
