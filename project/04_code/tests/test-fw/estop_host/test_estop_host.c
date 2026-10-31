/* Host test of the REAL tfw_estop_io.c (no ESP-IDF): the IDF calls are replaced by controllable stubs (../idf_stub/stub_idf.h
 * declares them). Review round 2: N-02 (init failure must block motion) and N-03 (the ISR latches BEFORE it stops).
 * Proves the control flow only; ISR timing and the real driver behaviour remain HIL items. */
#include <stdio.h>
#include <string.h>
#include "tfw_estop_io.h"
#include "tfw_pulse2.h"
#include "tfw_cfg.h"

static int g_run, g_fail;
#define CHECK(c) do { g_run++; if (!(c)) { g_fail++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---- controllable stubs ---- */
static int oe_writes;     /* the e-stop code must never write the shared pulse output enable (GPIO33) */
static int fail_gpio_config, fail_install, fail_handler_add, fail_task, install_state_err, fail_fault_new;
static int level = 1;                       /* axis 1 STOP input, active-LOW: 1 = inactive */
static int level2 = 1;                      /* axis 2 STOP input */
static int start_lv[64];                    /* START pin levels by gpio number (1 = inactive for active-LOW) */
static void (*isr2)(void *); static void *isr_arg[2];
static void (*isr)(void *);
static int latched_when_stopped = -1, stop_calls;
static uint32_t cyc;
const char *esp_err_to_name(esp_err_t e) { (void)e; return "E"; }
uint32_t esp_cpu_get_cycle_count(void) { return cyc += 400; }
int esp_cpu_get_core_id(void) { return 0; }
esp_err_t gpio_config(const gpio_config_t *c) { (void)c; return fail_gpio_config ? ESP_FAIL : ESP_OK; }
int gpio_get_level(gpio_num_t g)
{
    if (g == 19) return level;
    if (g == 17) return level2;
    return start_lv[g & 63];
}
esp_err_t gpio_set_level(gpio_num_t g, uint32_t v) { (void)v; if (g == 33) oe_writes++; return ESP_OK; }
esp_err_t gpio_install_isr_service(int f) { (void)f; return fail_install ? ESP_FAIL : install_state_err ? ESP_ERR_INVALID_STATE : ESP_OK; }
esp_err_t gpio_isr_handler_add(gpio_num_t g, void (*fn)(void *), void *a)
{
    if (fail_handler_add) return ESP_FAIL;
    isr = fn;
    if (g == 19) isr_arg[0] = a; else isr_arg[1] = a;
    if (g == 17) isr2 = fn;
    return ESP_OK;
}
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
static int stopped_axis[8], n_stopped;
void tfw_pulse2_stop(uint8_t ax) { if (n_stopped < 8) stopped_axis[n_stopped++] = ax; stop_calls++; latched_when_stopped = tfw_estop_latched_axis(ax) ? 1 : 0; }
static int start_calls, start_last = -1;
static void on_start(uint8_t ax) { start_calls++; start_last = ax; }
static void run_ms(int n) { for (int i = 0; i < n; ++i) tfw_estop_poll_once(); }

static void reset(void)
{
    fail_gpio_config = fail_install = fail_handler_add = fail_task = install_state_err = fail_fault_new = 0;
    level = 1; level2 = 1; isr = NULL; isr2 = NULL; latched_when_stopped = -1; stop_calls = 0;
    n_stopped = 0; oe_writes = 0; start_calls = 0; start_last = -1; start_lv[54] = start_lv[18] = 1;     /* START active LOW: 1 = open */
    isr_arg[0] = isr_arg[1] = NULL;
}
static bool g_active_low = true;
static int g_pins[2] = {19, 17};
static esp_err_t init(void) { mcpwm_oper_handle_t o[2] = {NULL, NULL}; tfw_estop_set_start_cb(on_start); return tfw_estop_init(g_pins, g_active_low, o); }
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
    reset(); g_active_low = true; level = 0;
    CHECK(init() == ESP_OK); CHECK(tfw_estop_blocking() && tfw_estop_latched());
}
static void t_isr_latches_before_it_stops(void)
{
    reset(); CHECK(init() == ESP_OK); CHECK(isr != NULL);
    level = 0; isr(isr_arg[0]);
    CHECK(stop_calls == 1 && stopped_axis[0] == 0);
    CHECK(latched_when_stopped == 1);            /* N-03: a task re-checking after its timer start sees the latch */
    CHECK(tfw_estop_blocking());
}

/* ---- baseline v0.4 behaviour (per-axis STOP, START edge). Default build: STOP NC = active HIGH, START active LOW. */
static void press_start(int ax, int pressed) { start_lv[ax == 0 ? 54 : 18] = pressed ? 0 : 1; }

static void t_stop_is_per_axis(void)
{
    reset(); g_active_low = TFW_B_ESTOP_ACTIVE_LOW; level = level2 = TFW_B_ESTOP_ACTIVE_LOW ? 1 : 0;
    CHECK(init() == ESP_OK); CHECK(isr_arg[0] == (void *)0 && isr_arg[1] == (void *)1);
    int act = TFW_B_ESTOP_ACTIVE_LOW ? 0 : 1;
    level2 = act; isr(isr_arg[1]);                        /* axis 2 STOP fires */
    CHECK(n_stopped == 1 && stopped_axis[0] == 1);        /* only axis 2 was stopped, axis 1 was not */
    CHECK(tfw_estop_blocking_axis(1) && !tfw_estop_blocking_axis(0));
    CHECK(tfw_estop_latched_axis(1) && !tfw_estop_latched_axis(0));
    CHECK(oe_writes == 0);                                /* shared pulse output enable GPIO33 untouched */
    n_stopped = 0; level = act; isr(isr_arg[0]);          /* now axis 1 */
    CHECK(n_stopped == 1 && stopped_axis[0] == 0);
    CHECK(oe_writes == 0);
}
static void t_stop_release_is_per_axis(void)
{
    reset(); g_active_low = TFW_B_ESTOP_ACTIVE_LOW; int act = TFW_B_ESTOP_ACTIVE_LOW ? 0 : 1, idle = !act;
    level = level2 = idle; CHECK(init() == ESP_OK);
    level = act; isr(isr_arg[0]); level2 = act; isr(isr_arg[1]);
    level2 = idle; run_ms(20);                            /* axis 2 released and stable, axis 1 still active */
    CHECK(tfw_estop_release_axis(1) == 0);
    CHECK(tfw_estop_latched_axis(0) && !tfw_estop_latched_axis(1));
    CHECK(tfw_estop_release_axis(0) != 0);                /* input of axis 1 still active: refused */
}
static void t_start_edge_starts_that_axis(void)
{
    reset(); g_active_low = TFW_B_ESTOP_ACTIVE_LOW; level = level2 = TFW_B_ESTOP_ACTIVE_LOW ? 1 : 0;
    CHECK(init() == ESP_OK);
    press_start(0, 1); run_ms(3); CHECK(start_calls == 0);      /* shorter than the debounce: no edge yet */
    run_ms(10);
    CHECK(start_calls == 1 && start_last == 0);                  /* edge accepted once */
    run_ms(50); CHECK(start_calls == 1);                         /* held level is not another edge */
    press_start(0, 0); run_ms(20); CHECK(start_calls == 1);      /* release is not an edge */
    press_start(0, 1); run_ms(20); CHECK(start_calls == 2);      /* next press = next edge */
}
static void t_start_held_at_boot_is_not_an_edge(void)
{
    reset(); g_active_low = TFW_B_ESTOP_ACTIVE_LOW; level = level2 = TFW_B_ESTOP_ACTIVE_LOW ? 1 : 0;
    press_start(0, 1); CHECK(init() == ESP_OK);
    run_ms(50); CHECK(start_calls == 0);
}
static void t_start_ignored_while_stop_active_and_needs_new_edge(void)
{
    reset(); g_active_low = TFW_B_ESTOP_ACTIVE_LOW; int act = TFW_B_ESTOP_ACTIVE_LOW ? 0 : 1, idle = !act;
    level = level2 = idle; CHECK(init() == ESP_OK);
    level = act; isr(isr_arg[0]);                                 /* axis 1 STOP active */
    press_start(0, 1); run_ms(30);
    CHECK(start_calls == 0);                                      /* START during STOP is ignored */
    level = idle; run_ms(30);                                     /* STOP released while START is still held */
    CHECK(start_calls == 0);                                      /* holding START does not restart */
    press_start(0, 0); run_ms(20); press_start(0, 1); run_ms(20);
    CHECK(start_calls == 1 && start_last == 0);                   /* a new edge re-arms the latched axis and starts it */
    CHECK(!tfw_estop_latched_axis(0));
}
static void t_start_of_other_axis_unaffected_by_stop(void)
{
    reset(); g_active_low = TFW_B_ESTOP_ACTIVE_LOW; int act = TFW_B_ESTOP_ACTIVE_LOW ? 0 : 1, idle = !act;
    level = level2 = idle; CHECK(init() == ESP_OK);
    level = act; isr(isr_arg[0]);                                 /* axis 1 STOP active */
    if (TFW_B_AX2_IO_PRESENT) { press_start(1, 1); run_ms(20); CHECK(start_calls == 1 && start_last == 1); }
    else { press_start(1, 1); run_ms(20); CHECK(start_calls == 0); }   /* D-068: axis 2 START pin is not even read */
}
static void t_ax2_io_absent_is_ignored(void)
{
    reset(); g_active_low = TFW_B_ESTOP_ACTIVE_LOW; level = TFW_B_ESTOP_ACTIVE_LOW ? 1 : 0;
    int pins[2] = {19, -1}; mcpwm_oper_handle_t o[2] = {NULL, NULL};
    tfw_estop_set_start_cb(on_start);
    CHECK(tfw_estop_init(pins, g_active_low, o) == ESP_OK);
    level2 = TFW_B_ESTOP_ACTIVE_LOW ? 0 : 1;                      /* floating / active input on the unused axis 2 pin */
    run_ms(20);
    CHECK(!tfw_estop_blocking_axis(1));                           /* never blocks axis 2 */
    CHECK(!tfw_estop_blocking());
    CHECK(tfw_estop_blocking_axis(0) == false);
}
static void t_boot_level_and_state_report_both_axes(void)
{
    reset(); g_active_low = TFW_B_ESTOP_ACTIVE_LOW; int act = TFW_B_ESTOP_ACTIVE_LOW ? 0 : 1, idle = !act;
    level = idle; level2 = act; CHECK(init() == ESP_OK);          /* axis 2 active at boot */
    tfw_es_stats_t st; tfw_estop_stats(&st);
    CHECK(st.state >= 1);
    CHECK(tfw_estop_latched_axis(1) && !tfw_estop_latched_axis(0));
    CHECK(((st.boot_level >> 1) & 1u) == (unsigned)level2 && (st.boot_level & 1u) == (unsigned)level);
}

int main(void)
{
    t_not_initialised_blocks();                  /* must run first: s_init_ok is still its initial value */
    t_init_ok_does_not_block(); t_each_init_failure_blocks(); t_service_already_installed_is_exported();
    t_active_at_boot_blocks(); t_isr_latches_before_it_stops();
    t_stop_is_per_axis(); t_stop_release_is_per_axis(); t_start_edge_starts_that_axis(); t_start_held_at_boot_is_not_an_edge();
    t_start_ignored_while_stop_active_and_needs_new_edge(); t_start_of_other_axis_unaffected_by_stop();
    t_ax2_io_absent_is_ignored(); t_boot_level_and_state_report_both_axes();
    printf("estop_host: checks=%d failed=%d\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
