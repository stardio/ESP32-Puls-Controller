/* Host test of the REAL tfw_estop_io.c (no ESP-IDF): the IDF calls are replaced by controllable stubs (../idf_stub/stub_idf.h
 * declares them). Review round 2: N-02 (init failure must block motion) and N-03 (the ISR latches BEFORE it stops).
 * Proves the control flow only; ISR timing and the real driver behaviour remain HIL items. */
#include <stdio.h>
#include <string.h>
#include "tfw_estop_io.h"
#include "tfw_pulse2.h"

static int g_run, g_fail;
#define CHECK(c) do { g_run++; if (!(c)) { g_fail++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---- controllable stubs ---- */
static int fail_gpio_config, fail_install, fail_handler_add, fail_task, install_state_err, fail_fault_new;
static int level = 1;                       /* active-LOW input: 1 = inactive */
static void (*isr)(void *);
static int latched_when_stopped = -1, stop_calls;
static uint32_t cyc;
const char *esp_err_to_name(esp_err_t e) { (void)e; return "E"; }
uint32_t esp_cpu_get_cycle_count(void) { return cyc += 400; }
int esp_cpu_get_core_id(void) { return 0; }
esp_err_t gpio_config(const gpio_config_t *c) { (void)c; return fail_gpio_config ? ESP_FAIL : ESP_OK; }
int gpio_get_level(gpio_num_t g) { (void)g; return level; }
esp_err_t gpio_set_level(gpio_num_t g, uint32_t v) { (void)g; (void)v; return ESP_OK; }
esp_err_t gpio_install_isr_service(int f) { (void)f; return fail_install ? ESP_FAIL : install_state_err ? ESP_ERR_INVALID_STATE : ESP_OK; }
esp_err_t gpio_isr_handler_add(gpio_num_t g, void (*fn)(void *), void *a) { (void)g; (void)a; if (fail_handler_add) return ESP_FAIL; isr = fn; return ESP_OK; }
esp_err_t mcpwm_new_gpio_fault(const mcpwm_gpio_fault_config_t *c, mcpwm_fault_handle_t *f) { (void)c; (void)f; return fail_fault_new ? ESP_FAIL : ESP_OK; }
esp_err_t mcpwm_operator_set_brake_on_fault(mcpwm_oper_handle_t o, const mcpwm_brake_config_t *b) { (void)o; (void)b; return ESP_OK; }
esp_err_t mcpwm_operator_recover_from_fault(mcpwm_oper_handle_t o, mcpwm_fault_handle_t f) { (void)o; (void)f; return ESP_OK; }
esp_err_t mcpwm_generator_set_action_on_brake_event(mcpwm_gen_handle_t g, mcpwm_gen_brake_event_action_t a) { (void)g; (void)a; return ESP_OK; }
BaseType_t xTaskCreate(void (*fn)(void *), const char *n, uint32_t st, void *a, int p, TaskHandle_t *h) { (void)fn; (void)n; (void)st; (void)a; (void)p; (void)h; return fail_task ? 0 : pdPASS; }
void vTaskDelay(TickType_t t) { (void)t; }
/* pulse2 side used by the e-stop code */
uint32_t tfw_pulse2_count_isr(uint8_t ax) { (void)ax; return 100; }
uint32_t tfw_pulse2_count(uint8_t ax) { (void)ax; return 130; }
mcpwm_gen_handle_t tfw_pulse2_generator(uint8_t ax) { (void)ax; return NULL; }
void tfw_pulse2_stop(uint8_t ax) { (void)ax; stop_calls++; latched_when_stopped = tfw_estop_latched() ? 1 : 0; }

static void reset(void)
{
    fail_gpio_config = fail_install = fail_handler_add = fail_task = install_state_err = fail_fault_new = 0;
    level = 1; isr = NULL; latched_when_stopped = -1; stop_calls = 0;
}
static esp_err_t init(void) { mcpwm_oper_handle_t o[2] = {NULL, NULL}; return tfw_estop_init(19, true, o); }
static uint16_t last_err(void) { tfw_es_stats_t s; tfw_estop_stats(&s); return s.last_err; }

static void t_not_initialised_blocks(void)
{
    CHECK(tfw_estop_blocking());                 /* before any init: round 2 code returned false -> motion allowed */
}
static void t_init_ok_does_not_block(void)
{
    reset(); CHECK(init() == ESP_OK); CHECK(!tfw_estop_blocking()); CHECK(last_err() == 0); CHECK(isr != NULL);
}
static void t_each_init_failure_blocks(void)
{
    int *flags[] = {&fail_gpio_config, &fail_install, &fail_handler_add, &fail_task};
    for (unsigned i = 0; i < 4; ++i) {
        reset(); *flags[i] = 1;
        CHECK(init() != ESP_OK);
        CHECK(tfw_estop_blocking());             /* N-02: RUN / legacy START are refused (0x10) */
        CHECK(last_err() != 0);
    }
    reset(); CHECK(init() == ESP_OK); CHECK(!tfw_estop_blocking());       /* a later successful init clears it */
}
static void t_service_already_installed_is_exported(void)
{
    reset(); install_state_err = 1;
    CHECK(init() == ESP_OK);
    CHECK(last_err() == ESP_ERR_INVALID_STATE);  /* M-03 precondition not met: visible in es_last_err (T-07 -> NOT_RUN) */
    CHECK(!tfw_estop_blocking());
}
static void t_active_at_boot_blocks(void)
{
    reset(); level = 0;
    CHECK(init() == ESP_OK); CHECK(tfw_estop_blocking() && tfw_estop_latched());
}
static void t_isr_latches_before_it_stops(void)
{
    reset(); CHECK(init() == ESP_OK); CHECK(isr != NULL);
    level = 0; isr(NULL);
    CHECK(stop_calls == 2);
    CHECK(latched_when_stopped == 1);            /* N-03: a task re-checking after its timer start sees the latch */
    CHECK(tfw_estop_blocking());
}

int main(void)
{
    t_not_initialised_blocks();                  /* must run first: s_init_ok is still its initial value */
    t_init_ok_does_not_block(); t_each_init_failure_blocks(); t_service_already_installed_is_exported();
    t_active_at_boot_blocks(); t_isr_latches_before_it_stops();
    printf("estop_host: checks=%d failed=%d\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
