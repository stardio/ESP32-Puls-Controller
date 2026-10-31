#include "tfw_logic.h"
#include <math.h>

bool tfw_res_valid(uint32_t r)
{
    return r == 2000000u || r == 10000000u || r == 20000000u || r == 40000000u;
}

/* Source: servo1axis components/movion_modbus/movion_modbus_tcp.c calculate_profile() [E2].
 * Differences: state is per instance (two axes), ISR-side data is float32 (see tfw_profile_freq). */
void tfw_profile_calc(tfw_profile_t *p, uint32_t position, uint32_t start_speed, uint32_t target_speed,
                      uint32_t accel_ms, uint32_t decel_ms)
{
    p->position = position;
    p->start_speed = start_speed;
    p->target_speed = target_speed;
    p->start2 = (float)((double)start_speed * start_speed);
    if (accel_ms == 0) accel_ms = 1;
    if (decel_ms == 0) decel_ms = 1;
    if (target_speed <= start_speed) {
        p->peak = (float)start_speed;
        p->accel_pulses = 0;
        p->decel_start = position;
        p->k_acc = p->k_dec = 0.0f;
        return;
    }
    const double accel = ((double)target_speed - start_speed) * 1000.0 / accel_ms;
    const double decel = ((double)target_speed - start_speed) * 1000.0 / decel_ms;
    p->k_acc = (float)(2.0 * accel);
    p->k_dec = (float)(2.0 * decel);
    const double s2 = (double)start_speed * start_speed;
    const double t2 = (double)target_speed * target_speed;
    const double full_accel = (t2 - s2) / (2.0 * accel);
    const double full_decel = (t2 - s2) / (2.0 * decel);
    uint32_t decel_pulses;
    if (full_accel + full_decel <= position) {
        p->peak = (float)target_speed;
        p->accel_pulses = (uint32_t)full_accel;
        decel_pulses = (uint32_t)full_decel;
    } else {
        const double inv = 1.0 / accel + 1.0 / decel;
        const double peak = sqrt(s2 + 2.0 * position / inv);
        p->peak = (float)peak;
        p->accel_pulses = (uint32_t)((peak * peak - s2) / (2.0 * accel));
        decel_pulses = position - p->accel_pulses;
    }
    p->decel_start = position - decel_pulses;
}

uint32_t tfw_rand_next(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x;
    return x;
}

uint32_t tfw_rand_distance(uint32_t *s)
{
    static const uint32_t set[] = {1u, 2u, 1000u, 29999u, 30000u, 30001u, 1000000u};
    return set[tfw_rand_next(s) % (sizeof(set) / sizeof(set[0]))];
}

void tfw_rand_motion(uint32_t *s, uint32_t max_speed, uint32_t *speed, uint32_t *acc, uint32_t *dec)
{
    uint32_t lo = max_speed / 10u;
    if (lo < 100u) lo = 100u;
    if (lo > max_speed) lo = max_speed;
    *speed = lo + tfw_rand_next(s) % (max_speed - lo + 1u);
    *acc = 10u + tfw_rand_next(s) % 191u;     /* 10..200 ms [assumed] */
    *dec = 10u + tfw_rand_next(s) % 191u;
}

uint32_t tfw_load_busy_ms(uint32_t pct, uint32_t period_ms)
{
    if (pct > 90u) pct = 90u;
    return pct * period_ms / 100u;
}

/* ---- emergency stop latch ---- */
void tfw_es_init(tfw_es_t *e) { e->latched = 0; e->path = 3; e->trip_count = 0; e->snap[0] = e->snap[1] = 0; e->stable_ms = 0; }
bool tfw_es_path_valid(uint8_t sel) { return sel >= 1 && sel <= 3; }
void tfw_es_boot_check(tfw_es_t *e, bool input_active)
{
    if (input_active && !e->latched) tfw_es_trip(e, 0u, 0u);
}
uint16_t tfw_es_state(const tfw_es_t *e, bool input_active)
{
    if (input_active) return 1;
    return e->latched ? 2 : 0;
}
void tfw_es_input_sample(tfw_es_t *e, bool input_active, uint32_t dt_ms)
{
    if (input_active) e->stable_ms = 0;
    else if (e->stable_ms < 0xFFFFFFF0u) e->stable_ms += dt_ms;
}
uint16_t tfw_es_release(tfw_es_t *e, bool input_active)
{
    if (input_active || e->stable_ms < TFW_ES_RELEASE_STABLE_MS) return TFW_ST_REJECT_BUSY;
    e->latched = 0;       /* no automatic restart: nothing else is touched */
    return 0;
}
uint32_t tfw_es_pulses_after(uint32_t snap, uint32_t final_count) { return final_count > snap ? final_count - snap : 0u; }

unsigned tfw_hist_bin(uint32_t us)
{
    static const uint32_t lim[7] = {50, 100, 200, 500, 1000, 2000, 5000};
    for (unsigned i = 0; i < 7; ++i) if (us < lim[i]) return i;
    return 7;
}

const int tfw_reserved_pins[TFW_RESERVED_PIN_COUNT] = {51, 17, 18, 19, 54, 23, 26, 27, 46, 24, 47, 48, 33};

void tfw_start_init(tfw_start_t *s, bool initial_active) { s->stable = initial_active; s->cand_ms = 0; }
bool tfw_start_sample(tfw_start_t *s, bool raw_active, bool stop_active, uint32_t dt_ms, uint32_t debounce_ms)
{
    if (raw_active == s->stable) { s->cand_ms = 0; return false; }
    if (s->cand_ms < 0xFFFFFFF0u) s->cand_ms += dt_ms;
    if (s->cand_ms < debounce_ms) return false;
    s->stable = raw_active; s->cand_ms = 0;
    return raw_active && !stop_active;          /* only the rising (inactive -> active) transition, and not while STOP */
}

bool tfw_pin_conflict(const int *a, size_t na, const int *b, size_t nb)
{
    for (size_t i = 0; i < na; ++i) {
        if (a[i] < 0) continue;
        for (size_t j = i + 1; j < na; ++j) if (a[j] == a[i]) return true;
        for (size_t j = 0; j < nb; ++j) if (b[j] == a[i]) return true;
    }
    return false;
}
