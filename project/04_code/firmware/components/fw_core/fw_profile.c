#include "fw_profile.h"
#include <math.h>

bool fw_res_valid(uint32_t r)
{
    return r == 2000000u || r == 10000000u || r == 20000000u || r == 40000000u;
}

void fw_profile_calc(fw_profile_t *p, uint32_t position, uint32_t start_speed, uint32_t target_speed,
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

bool fw_profile_plan_stop(fw_profile_t *p, uint32_t n, uint32_t f_now, uint32_t *stop_at)
{
    if (f_now < p->start_speed) f_now = p->start_speed;
    uint32_t d = 0;
    if (f_now > p->start_speed && p->k_dec > 0.0f) {
        const double num = (double)f_now * f_now - (double)p->start_speed * p->start_speed;
        d = (uint32_t)(num / (double)p->k_dec);
    }
    const uint64_t end = (uint64_t)n + d;
    if (end >= p->position) return false;          /* original profile ends first: leave it alone */
    p->position = (uint32_t)end;
    p->decel_start = n;
    p->accel_pulses = 0;
    p->peak = (float)f_now;
    p->target_speed = f_now;
    *stop_at = (uint32_t)end;
    return true;
}

uint32_t fw_crc32(const void *data, size_t len)
{
    const uint8_t *b = (const uint8_t *)data;
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        c ^= b[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1u)));
    }
    return ~c;
}

uint16_t fw_crc16_modbus(const uint8_t *data, size_t len)
{
    uint16_t c = 0xFFFFu;
    for (size_t i = 0; i < len; ++i) {
        c ^= data[i];
        for (int k = 0; k < 8; ++k) c = (c & 1u) ? (uint16_t)((c >> 1) ^ 0xA001u) : (uint16_t)(c >> 1);
    }
    return c;
}
