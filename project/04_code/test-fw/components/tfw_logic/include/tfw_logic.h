/* tfw_logic: hardware-independent logic of the G3 test firmware (D-025).
 * No ESP-IDF includes: built with gcc on the host for unit tests and with
 * ESP-IDF for the firmware. Functions marked TFW_INLINE are called from ISR
 * context on the target: integer / single-precision float only, no libm,
 * no 64-bit division, no RTOS calls. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tfw_regs.h"

#define TFW_INLINE static inline __attribute__((always_inline))
#define TFW_PCNT_HIGH_LIMIT   30000u
#define TFW_AUTO_START_SPEED  5000u
#define TFW_MAX_PERIOD_TICKS  65535u   /* [assumed] MCPWM timer period width; probe_caps records the real value */

/* ---------- frequency quantisation (risk 1: never exceed the request) ---------- */
/* ticks = ceil(res/f), min 2. Returns false if f == 0, res == 0 or ticks > max_ticks
 * (the caller must reject: clamping would output a HIGHER frequency than requested). */
TFW_INLINE bool tfw_ticks_for_freq(uint32_t res_hz, uint32_t f_hz, uint32_t max_ticks, uint32_t *ticks)
{
    if (f_hz == 0 || res_hz == 0) return false;
    uint32_t t = res_hz / f_hz + ((res_hz % f_hz) ? 1u : 0u);
    if (t < 2u) t = 2u;
    if (t > max_ticks) return false;
    *ticks = t;
    return true;
}
TFW_INLINE uint32_t tfw_freq_actual(uint32_t res_hz, uint32_t ticks) { return ticks ? res_hz / ticks : 0u; }
TFW_INLINE uint32_t tfw_cmp_for_ticks(uint32_t ticks) { return ticks / 2u; }   /* >= 1 for ticks >= 2 */
bool tfw_res_valid(uint32_t res_hz);            /* 2M/10M/20M/40M */

/* ---------- PCNT accumulation (30,000 blocks) ---------- */
TFW_INLINE uint32_t tfw_total_count(uint32_t completed, int pcnt)
{
    uint64_t t = (uint64_t)completed + (pcnt > 0 ? (uint32_t)pcnt : 0u);
    return t > UINT32_MAX ? UINT32_MAX : (uint32_t)t;
}
TFW_INLINE uint32_t tfw_target_remainder(uint32_t target) { return target % TFW_PCNT_HIGH_LIMIT; }

/* ---------- trapezoid / triangle profile (copied from servo1axis calculate_profile/profile_frequency [E2]) ---------- */
typedef struct {
    uint32_t position, start_speed, target_speed;
    uint32_t accel_pulses, decel_start;
    float start2, k_acc, k_dec, peak;   /* float32: ISR-safe on a core with FPU */
} tfw_profile_t;
TFW_INLINE uint32_t tfw_start_speed(uint32_t speed) { return speed < TFW_AUTO_START_SPEED ? speed : TFW_AUTO_START_SPEED; }
void tfw_profile_calc(tfw_profile_t *p, uint32_t position, uint32_t start_speed, uint32_t target_speed,
                      uint32_t accel_ms, uint32_t decel_ms);   /* task context (uses double / libm) */
TFW_INLINE uint32_t tfw_profile_freq(const tfw_profile_t *p, uint32_t n)
{
    float f = p->peak;
    if (n < p->accel_pulses) {
        f = __builtin_sqrtf(p->start2 + p->k_acc * (float)n);
    } else if (n >= p->decel_start) {
        uint32_t rem = p->position > n ? p->position - n : 0u;
        f = __builtin_sqrtf(p->start2 + p->k_dec * (float)rem);
    }
    if (f < (float)p->start_speed) f = (float)p->start_speed;
    if (f > (float)p->target_speed) f = (float)p->target_speed;
    return (uint32_t)f;
}

/* ---------- update-callback statistics (ISR-callable) ---------- */
typedef struct { uint32_t max_us, late, calls, last_start; } tfw_upd_stats_t;
/* cycles are 32-bit CPU cycle-counter values; late = interval > 1.5 x period [assumed] */
TFW_INLINE void tfw_upd_record(tfw_upd_stats_t *s, uint32_t c_start, uint32_t c_end, uint32_t cpu_mhz, uint32_t period_us)
{
    uint32_t dur = (c_end - c_start) / cpu_mhz;
    if (dur > s->max_us) s->max_us = dur;
    if (s->calls > 0) {
        uint32_t interval = (c_start - s->last_start) / cpu_mhz;
        if (interval > period_us + period_us / 2u) s->late++;
    }
    s->last_start = c_start;
    s->calls++;
}

/* ---------- result judging (TFW-01 sec.3 RUN_SEQ) ---------- */
typedef struct { uint32_t err, overshoot; } tfw_judge_t;
TFW_INLINE void tfw_judge_move(tfw_judge_t *j, uint32_t target, uint32_t pcnt)
{
    if (pcnt != target) j->err++;
    if (pcnt > target) j->overshoot += pcnt - target;
}
uint32_t tfw_rand_next(uint32_t *state);                 /* xorshift32 */
uint32_t tfw_rand_distance(uint32_t *state);             /* T-04 distance set */
void     tfw_rand_motion(uint32_t *state, uint32_t max_speed, uint32_t *speed, uint32_t *accel_ms, uint32_t *decel_ms);
uint32_t tfw_load_busy_ms(uint32_t pct, uint32_t period_ms);

/* ---------- results snapshot (single-writer fields) ---------- */
typedef struct {
    uint32_t seq_done, err[2], overshoot[2];
    uint32_t upd_late, upd_max_us, isr_lat_ns[2];
    uint32_t nvs_writes, nvs_max_ms;
    uint16_t chip_rev, cpu_mhz;
    uint32_t last_target[2], last_pcnt[2];
    uint16_t mb_clients, mb_link_down;
    uint32_t mb_req_count, mb_proc_max_us;
    uint16_t mb_hist[8];
    uint16_t es_state, es_trip_count;
    uint32_t es_isr_lat_ns, es_pulses_after[2];
    uint16_t es_bind_ok, es_boot_level, es_last_err;
} tfw_results_t;

/* ---------- test register window ---------- */
typedef struct {
    uint16_t axis_mask;
    uint32_t freq, count;
    uint16_t accel_ms, decel_ms;
    uint32_t repeat, seed;
    uint16_t load_nvs_ms, load_cpu_pct, cfg_upd_us;
    uint32_t cfg_res_hz;
    uint16_t cfg_flags;
    uint16_t es_path_sel;
    volatile uint16_t status;
} tfw_regwin_t;

typedef struct {
    uint16_t cmd;            /* 0 = none */
    bool     es_path_set;    /* es_path_sel written (value in win->es_path_sel) */
    bool     es_release;
    bool     cfg_changed;    /* 0x700E/0x7010/0x7012 written: persist for next boot */
    bool     load_changed;   /* 0x700C/0x700D written */
} tfw_wr_effect_t;

void tfw_regwin_init(tfw_regwin_t *w, uint16_t build_flags);
/* returns 0 or a Modbus exception code (2 illegal address, 3 partial 32-bit write).
 * An out-of-range value is NOT an exception: it is ignored and win->status = 0x11. */
int  tfw_regwin_write(tfw_regwin_t *w, uint16_t addr, uint16_t count, const uint16_t *v, tfw_wr_effect_t *fx);
int  tfw_regwin_read(const tfw_regwin_t *w, const tfw_results_t *r, uint16_t addr, uint16_t count, uint16_t *out);
/* returns 0 if the command may run, else a tst_status error code */
uint16_t tfw_cmd_check(const tfw_regwin_t *w, uint16_t cmd, bool running, bool estop_latched, bool ax2_ok);

/* ---------- sequencer (hardware behind a vtable) ---------- */
typedef struct {
    void     *ctx;
    int      (*move)(void *ctx, uint8_t axis, uint32_t speed, uint32_t count, uint32_t acc_ms, uint32_t dec_ms);
    int      (*fixed)(void *ctx, uint8_t axis, uint32_t freq, uint32_t count);
    bool     (*busy)(void *ctx, uint8_t axis);
    uint32_t (*count)(void *ctx, uint8_t axis);
    void     (*stop)(void *ctx, uint8_t axis);
    void     (*delay_ms)(void *ctx, uint32_t ms);
    uint32_t (*now_ms)(void *ctx);
    bool     (*aborted)(void *ctx);
} tfw_seq_hw_t;

/* RUN_SEQ / RUN_MOVE / RUN_FIXED. Updates res (single writer) and w->status.
 * Returns number of completed iterations. */
uint32_t tfw_seq_run(const tfw_seq_hw_t *hw, tfw_regwin_t *w, volatile tfw_results_t *res, uint16_t cmd);

/* ---------- emergency stop latch logic (TFW-04) ---------- */
typedef struct {
    volatile uint8_t latched;
    uint8_t  path;                 /* 1 A, 2 B, 3 A+B */
    uint16_t trip_count;
    uint32_t snap[2];
    uint32_t stable_ms;            /* how long the input has been inactive */
} tfw_es_t;
#define TFW_ES_RELEASE_STABLE_MS 10u   /* [assumed] */
void     tfw_es_init(tfw_es_t *e);
bool     tfw_es_path_valid(uint8_t sel);
void     tfw_es_trip(tfw_es_t *e, uint32_t c0, uint32_t c1);
uint16_t tfw_es_state(const tfw_es_t *e, bool input_active);
uint16_t tfw_es_release(tfw_es_t *e, bool input_active);   /* 0 ok, TFW_ST_REJECT_BUSY if refused */
void     tfw_es_input_sample(tfw_es_t *e, bool input_active, uint32_t dt_ms);
uint32_t tfw_es_pulses_after(uint32_t snap, uint32_t final_count);

/* ---------- Modbus ---------- */
#define TFW_HR_LEGACY_COUNT 32
#define TFW_COIL_COUNT 8
enum { TFW_COIL_START = 0, TFW_COIL_SERVO = 1, TFW_COIL_STOP_RESET = 2, TFW_COIL_DIR_FWD = 3 };
enum { TFW_HR_POS = 0, TFW_HR_SPEED = 2, TFW_HR_CUR = 6, TFW_HR_STATUS = 10, TFW_HR_RESULT = 13, TFW_HR_FAULT = 14,
       TFW_HR_CLIENTS = 15, TFW_HR_LINK = 16, TFW_HR_START_SPD = 18, TFW_HR_ACCEL = 20, TFW_HR_DECEL = 22 };

typedef struct {
    uint16_t hr[TFW_HR_LEGACY_COUNT];
    uint16_t coils;
    bool     dirty;           /* a persisted legacy value changed (caller decides about NVS) */
} tfw_legacy_t;
void     tfw_legacy_init(tfw_legacy_t *l);
uint32_t tfw_legacy_u32(const tfw_legacy_t *l, unsigned low);
void     tfw_legacy_store_u32(tfw_legacy_t *l, unsigned low, uint32_t v);
bool     tfw_legacy_valid(const uint16_t *hr);
void     tfw_legacy_refresh(tfw_legacy_t *l, bool link, unsigned clients, bool moving);
uint8_t  tfw_legacy_di(const tfw_legacy_t *l, bool link, unsigned clients, bool moving);

/* dispatcher: legacy window (HR 0..31, coils 0..7) + test window 0x7000..0x70FF */
typedef struct {
    tfw_legacy_t *legacy;
    tfw_regwin_t *win;
    void (*get_results)(void *user, tfw_results_t *out);
    void *user;
    bool link; unsigned clients; bool moving;      /* inputs for DI / status, set by the caller */
    tfw_wr_effect_t fx;                            /* accumulated by writes; caller consumes */
} tfw_mb_ctx_t;

#define TFW_MB_MAX_PDU 253
/* returns response PDU length (0 = no response). */
size_t tfw_mb_process(tfw_mb_ctx_t *c, const uint8_t *req, size_t len, uint8_t *resp);

/* processing-time histogram bins: <50,<100,<200,<500,<1k,<2k,<5k,>=5k us */
unsigned tfw_hist_bin(uint32_t us);
