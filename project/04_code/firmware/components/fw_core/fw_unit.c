#include "fw_unit.h"
#include "fw_params.h"

/* No __int128: riscv32 has none. 64x64 -> 128 multiply and 128 / 64 shift-subtract divide are written out below. */

static bool ratio(const uint16_t *par, uint64_t *num, uint64_t *den)
{
    const uint32_t mode = par[FW_P_UNIT_MODE];
    if (mode == FW_UM_PULSE) { *num = 1; *den = 1; return true; }
    const uint64_t ppr = fw_w32(par, FW_P_PULSES_PER_REV);
    const uint64_t gn = par[FW_P_GEAR_NUM], gd = par[FW_P_GEAR_DEN];
    const uint64_t travel = mode == FW_UM_MM ? fw_w32(par, FW_P_LEAD_UM) : FW_DEG_MDEG_PER_REV;
    if (ppr == 0 || gn == 0 || gd == 0 || travel == 0 || (mode != FW_UM_MM && mode != FW_UM_DEG)) return false;
    *num = ppr * gn;                  /* <= 1e7 * 1000 */
    *den = travel * gd;               /* <= 1e7 * 1000 */
    return true;
}

/* round(mag * a / b) with a 128-bit intermediate; false if the quotient does not fit 64 bits. b > 0 */
static bool muldiv(uint64_t mag, uint64_t a, uint64_t b, uint64_t *out)
{
    const uint64_t al = (uint32_t)mag, ah = mag >> 32, bl = (uint32_t)a, bh = a >> 32;
    const uint64_t ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
    const uint64_t mid = (ll >> 32) + (uint32_t)lh + (uint32_t)hl;
    uint64_t hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
    uint64_t lo = (mid << 32) | (uint32_t)ll;
    const uint64_t half = b / 2u;                     /* round half up on the magnitude = half away from zero */
    lo += half; if (lo < half) hi++;
    if (hi >= b) return false;                        /* quotient >= 2^64 */
    uint64_t rem = hi, q = 0;                         /* long division of (hi:lo) by b, hi < b */
    for (int i = 63; i >= 0; --i) {
        const bool carry = (rem >> 63) != 0;
        rem = (rem << 1) | ((lo >> i) & 1u);
        if (carry || rem >= b) { rem -= b; q |= 1ull << i; }
    }
    *out = q;
    return true;
}

static bool conv_signed(uint64_t num, uint64_t den, int64_t v, int32_t *out)
{
    const bool neg = v < 0;
    const uint64_t mag = neg ? (uint64_t)(-(v + 1)) + 1u : (uint64_t)v;
    uint64_t r;
    if (!muldiv(mag, num, den, &r)) return false;
    if (neg) { if (r > 2147483648ull) return false; *out = (int32_t)(0 - (int64_t)r); }
    else { if (r > 2147483647ull) return false; *out = (int32_t)r; }
    return true;
}

bool fw_unit_to_pulses(const uint16_t *par, int64_t unit, int32_t *pulses)
{
    uint64_t n, d;
    if (!ratio(par, &n, &d)) return false;
    return conv_signed(n, d, unit, pulses);
}
bool fw_pulses_to_unit(const uint16_t *par, int64_t pulses, int32_t *unit)
{
    uint64_t n, d;
    if (!ratio(par, &n, &d)) return false;
    return conv_signed(d, n, pulses, unit);
}
bool fw_unit_speed_to_hz(const uint16_t *par, uint32_t su, uint32_t *hz)
{
    uint64_t n, d, r;
    if (!ratio(par, &n, &d) || !muldiv(su, n, d, &r) || r > UINT32_MAX) return false;
    *hz = (uint32_t)r;
    return true;
}
bool fw_hz_to_unit(const uint16_t *par, uint32_t hz, uint32_t *su)
{
    uint64_t n, d, r;
    if (!ratio(par, &n, &d) || !muldiv(hz, d, n, &r) || r > UINT32_MAX) return false;
    *su = (uint32_t)r;
    return true;
}
int32_t fw_pulses_to_unit_sat(const uint16_t *par, int32_t pulses)
{
    int32_t u;
    if (fw_pulses_to_unit(par, pulses, &u)) return u;
    return pulses < 0 ? INT32_MIN : INT32_MAX;
}

static uint32_t speed_cap(const uint16_t *par)
{
    const uint32_t m = fw_w32(par, FW_P_MAX_SPEED);
    return m < FW_SPEED_MAX_HZ ? m : FW_SPEED_MAX_HZ;
}
uint16_t fw_max_rpm_calc(const uint16_t *par)
{
    const uint64_t ppr = fw_w32(par, FW_P_PULSES_PER_REV);
    if (ppr == 0) return 0;
    const uint64_t rpm = (uint64_t)speed_cap(par) * 60u / ppr;
    return rpm > 65535u ? 65535u : (uint16_t)rpm;
}
bool fw_test_hz(const uint16_t *par, uint32_t *hz)
{
    const uint64_t h = (uint64_t)par[FW_P_TEST_RPM] * fw_w32(par, FW_P_PULSES_PER_REV) / 60u;
    if (h < FW_SPEED_MIN_HZ || h > speed_cap(par)) return false;
    *hz = (uint32_t)h;
    return true;
}
