#include "fake_hal.h"
#include <string.h>

fake_pulse_t FP[2];
bool FAKE_SERVO[2], FAKE_OE, FAKE_IO_READY, FAKE_STOP_IN[2];
uint16_t FAKE_USER_DO;
fw_es_t FAKE_ES[2];
static fw_start_t ST[2];
static uint16_t EDGES[2], IGN[2];
static bool PREV_STOP[2];
int FAKE_START_CB_N[2];
uint32_t FAKE_NOW;
bool FAKE_RELEASE_FAIL, FAKE_NO_ISR, FAKE_FAULT_BOUND[2];
int FAKE_RELEASES;
static void (*CB)(uint8_t);

void fake_reset(void)
{
    memset(FP, 0, sizeof(FP)); memset(FAKE_SERVO, 0, sizeof(FAKE_SERVO)); memset(FAKE_STOP_IN, 0, sizeof(FAKE_STOP_IN));
    memset(PREV_STOP, 0, sizeof(PREV_STOP)); memset(FAKE_START_CB_N, 0, sizeof(FAKE_START_CB_N));
    FP[0].ready = FP[1].ready = true;
    FAKE_RELEASE_FAIL = false; FAKE_NO_ISR = false; FAKE_FAULT_BOUND[0] = FAKE_FAULT_BOUND[1] = true; FAKE_RELEASES = 0;
    FAKE_IO_READY = true; FAKE_OE = false; FAKE_USER_DO = 0; FAKE_NOW = 0;
    for (int i = 0; i < 2; ++i) { fw_es_init(&FAKE_ES[i]); fw_start_init(&ST[i], false); EDGES[i] = IGN[i] = 0; }
}

void fake_pulses(uint8_t a, uint32_t n)
{
    fake_pulse_t *p = &FP[a];
    if (!p->active) return;
    p->count += n;
    if (p->count >= p->target) { p->count = p->target; p->finished = true; }     /* ISR: output forced low at the target */
}
void fake_stop_isr(uint8_t a)
{
    if (!FAKE_ES[a].latched) fw_es_trip(&FAKE_ES[a], 0);
    FAKE_STOP_IN[a] = true;
    fw_hal_pulse_stop(a);
}
void fake_io_ms(uint8_t a, unsigned ms, bool stop_in, bool start_raw)
{
    for (unsigned i = 0; i < ms; ++i) {
        if (stop_in && !PREV_STOP[a] && !FAKE_NO_ISR) fake_stop_isr(a);
        PREV_STOP[a] = stop_in; FAKE_STOP_IN[a] = stop_in;
        if (fw_es_level_guard(&FAKE_ES[a], stop_in)) fw_hal_pulse_stop(a);            /* 1 ms level backup (M-02) */
        if (fw_stopstart_step(&FAKE_ES[a], &ST[a], stop_in, start_raw, 5, &EDGES[a], &IGN[a])) {
            FAKE_START_CB_N[a]++;
            if (CB) CB(a);
        }
    }
}

int fw_hal_pulse_init(uint8_t n, const uint32_t r[2]) { (void)n; (void)r; return 0; }
bool fw_hal_pulse_ready(uint8_t a) { return FP[a].ready; }
bool fw_hal_stop_blocking(uint8_t a);
int fw_hal_pulse_start(uint8_t a, uint32_t hz, uint32_t target)
{
    if (!FP[a].ready || !target) return FW_PE_BAD_ARG;
    if (FP[a].active) return FW_PE_BUSY;
    if (fw_hal_stop_blocking(a)) return FW_PE_STOP_BLOCK;
    FP[a].active = true; FP[a].finished = false; FP[a].ended = false; FP[a].count = 0; FP[a].target = target;
    FP[a].freq = FP[a].first_hz = hz; FP[a].starts++;
    return FW_PE_OK;
}
int fw_hal_pulse_set_freq(uint8_t a, uint32_t hz) { if (FP[a].fail_set_freq) return FW_PE_BAD_ARG; FP[a].freq = hz; return FW_PE_OK; }
void fw_hal_pulse_stop(uint8_t a) { if (FP[a].active) FP[a].stops++; FP[a].active = false; }
bool fw_hal_pulse_finished(uint8_t a) { return FP[a].active && FP[a].finished; }
void fw_hal_pulse_end(uint8_t a) { FP[a].active = false; FP[a].ended = true; FP[a].ends++; }
bool fw_hal_pulse_active(uint8_t a) { return FP[a].active; }
uint32_t fw_hal_pulse_count(uint8_t a) { return FP[a].count; }
void fw_hal_dir_set(uint8_t a, bool h) { FP[a].dir_high = h; }

void fw_hal_oe_early_forbid(void) { FAKE_OE = false; }
void fw_hal_oe_forbid(void) { FAKE_OE = false; }
void fw_hal_oe_allow(void) { FAKE_OE = true; }
bool fw_hal_oe_allowed(void) { return FAKE_OE; }
void fw_hal_servo_set(uint8_t a, bool on) { FAKE_SERVO[a] = on; }
void fw_hal_user_do_set(uint16_t m) { FAKE_USER_DO = m; }
uint16_t fw_hal_do_state(void) { return (uint16_t)((FAKE_SERVO[0] ? 1 : 0) | (FAKE_SERVO[1] ? 2 : 0) | (FAKE_USER_DO << 2)); }
uint16_t fw_hal_di_raw(void) { return 0; }

int fw_hal_io_init(uint8_t n) { (void)n; return 0; }
bool fw_hal_io_ready(void) { return FAKE_IO_READY; }
bool fw_hal_stop_input(uint8_t a) { return FAKE_STOP_IN[a]; }
bool fw_hal_stop_latched(uint8_t a) { return FAKE_ES[a].latched != 0; }
bool fw_hal_stop_blocking(uint8_t a) { return !FAKE_IO_READY || fw_es_blocking(&FAKE_ES[a], FAKE_STOP_IN[a]); }
uint16_t fw_hal_stop_state(uint8_t a) { return fw_es_state(&FAKE_ES[a], FAKE_STOP_IN[a]); }
uint16_t fw_hal_stop_trips(uint8_t a) { return FAKE_ES[a].trip_count; }
int fw_hal_stop_release(uint8_t a)
{
    if (FAKE_RELEASE_FAIL) return FW_ES_REFUSED;                  /* recover_from_fault failed: the latch stays */
    const uint16_t r = fw_es_release(&FAKE_ES[a], FAKE_STOP_IN[a]);
    if (r == 0) FAKE_RELEASES++;
    return r;
}
bool fw_hal_fault_bound(uint8_t a) { return FAKE_FAULT_BOUND[a]; }
void fw_hal_set_start_cb(void (*cb)(uint8_t)) { CB = cb; }

uint32_t fw_hal_ms(void) { return FAKE_NOW; }
void fw_hal_delay_us(uint32_t us) { (void)us; }
uint16_t fw_hal_reset_reason(void) { return 0; }
int fw_hal_wdt_init(void) { return 0; }
int fw_hal_wdt_add_self(void) { return 0; }
void fw_hal_wdt_feed(void) {}
void fw_hal_reboot(void) {}
void fw_hal_mac(uint8_t m[6]) { memset(m, 0, 6); }
int fw_hal_store_init(void) { return 0; }
int fw_hal_store_read(void *b, size_t *l) { (void)b; (void)l; return FW_ST_NOT_FOUND; }
int fw_hal_store_write(const void *b, size_t l) { (void)b; (void)l; return FW_ST_OK; }
