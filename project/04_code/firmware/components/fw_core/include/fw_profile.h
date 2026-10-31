/* fw_core / fw_profile: pulse timing and trapezoid profile maths (hardware independent, host-testable).
 * Source: project/04_code/test-fw/components/tfw_logic (tfw_core.c, tfw_logic.h; reviewed) which in turn copies
 * servo1axis calculate_profile()/profile_frequency(). Changes: renamed; manual start speed; decel-stop planning. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FW_INLINE static inline __attribute__((always_inline))
#define FW_PCNT_HIGH_LIMIT   30000u
#define FW_AUTO_START_SPEED  5000u      /* servo1axis: start speed = min(5000 Hz, speed) */
#define FW_MAX_PERIOD_TICKS  65535u     /* [assumed] MCPWM timer period width; the HAL may lower it */
#define FW_SPEED_MIN_HZ      100u       /* legacy range 100..1,000,000 */
#define FW_SPEED_MAX_HZ      1000000u

/* ticks = ceil(res/f), min 2. false if f==0, res==0 or ticks > max_ticks (reject: clamping would output MORE than requested) */
FW_INLINE bool fw_ticks_for_freq(uint32_t res_hz, uint32_t f_hz, uint32_t max_ticks, uint32_t *ticks)
{
    if (f_hz == 0 || res_hz == 0) return false;
    uint32_t t = res_hz / f_hz + ((res_hz % f_hz) ? 1u : 0u);
    if (t < 2u) t = 2u;
    if (t > max_ticks) return false;
    *ticks = t;
    return true;
}
FW_INLINE uint32_t fw_freq_actual(uint32_t res_hz, uint32_t ticks) { return ticks ? res_hz / ticks : 0u; }
FW_INLINE uint32_t fw_cmp_for_ticks(uint32_t ticks) { return ticks / 2u; }
bool fw_res_valid(uint32_t res_hz);             /* 2M / 10M / 20M / 40M */

/* PCNT 30,000-pulse block accumulation */
FW_INLINE uint32_t fw_total_count(uint32_t completed, int pcnt)
{
    uint64_t t = (uint64_t)completed + (pcnt > 0 ? (uint32_t)pcnt : 0u);
    return t > UINT32_MAX ? UINT32_MAX : (uint32_t)t;
}
FW_INLINE uint32_t fw_target_remainder(uint32_t target) { return target % FW_PCNT_HIGH_LIMIT; }

/* start speed: manual (non-zero) or automatic min(5000, speed); never above the move speed */
FW_INLINE uint32_t fw_start_speed(uint32_t manual, uint32_t speed)
{
    uint32_t s = manual ? manual : (speed < FW_AUTO_START_SPEED ? speed : FW_AUTO_START_SPEED);
    return s > speed ? speed : s;
}

typedef struct {
    uint32_t position, start_speed, target_speed;
    uint32_t accel_pulses, decel_start;
    float start2, k_acc, k_dec, peak;
} fw_profile_t;
void fw_profile_calc(fw_profile_t *p, uint32_t position, uint32_t start_speed, uint32_t target_speed,
                     uint32_t accel_ms, uint32_t decel_ms);              /* task context (double / libm) */
FW_INLINE uint32_t fw_profile_freq(const fw_profile_t *p, uint32_t n)
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
/* HMI stop (decelerate): re-plan a running profile so that it ends at n + d (d from the current frequency and the
 * configured deceleration). Returns false (profile untouched) if the stop point would be beyond the original end,
 * i.e. the move already ends sooner than a decelerated stop. *stop_at receives the new end count. */
bool fw_profile_plan_stop(fw_profile_t *p, uint32_t n, uint32_t f_now, uint32_t *stop_at);

/* plain integer helpers */
uint32_t fw_crc32(const void *data, size_t len);
uint16_t fw_crc16_modbus(const uint8_t *data, size_t len);
