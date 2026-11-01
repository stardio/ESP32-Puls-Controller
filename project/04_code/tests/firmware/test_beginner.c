/* Host tests of the beginner-usability features (interface-spec 5.12, SRS FR-029..FR-035, D-074..D-076):
 * unit conversion, setup_done/max_rpm_calc, wiring_status, TEST hold, alarm_hint, point table, resets, boot hold.
 * Uses the real register map + axis state machine over the fake HAL. [T-beg-n] */
#include <math.h>
#include <stdlib.h>
#include "fake_hal.h"
#include "fw_axis.h"
#include "fw_test.h"

static fw_dev_t D;
static fw_axis_t AX[2];
static int CFG_CHANGED, SYS_CMDS;

static uint8_t o_axis(void *c, uint8_t a, uint16_t cmd) { (void)c; return fw_axis_command(&AX[a], cmd); }
static uint16_t o_sys(void *c, uint16_t cmd)
{
    (void)c; SYS_CMDS++;
    if (cmd == FW_SYS_FACTORY_RESET || cmd == FW_SYS_NET_RESET) return fw_sys_reset_cmd(&D, cmd, fw_axis_running(&AX[0]) || fw_axis_running(&AX[1]));
    return FW_SRES_OK;
}
static void o_cfg(void *c) { (void)c; CFG_CHANGED++; }
static void on_start(uint8_t a) { fw_axis_start_physical(&AX[a]); }

static void setup(uint8_t axes, bool rtu)
{
    fake_reset(); CFG_CHANGED = SYS_CMDS = 0;
    fw_dev_init(&D, axes, rtu);
    D.ops = (fw_dev_ops_t){o_axis, NULL, NULL, o_sys, NULL, o_cfg, NULL};
    D.boot_status = FW_BOOT_PULSE_X | FW_BOOT_PULSE_Y | FW_BOOT_STOP_IO | FW_BOOT_NVS | FW_BOOT_CFG_OK | FW_BOOT_WDT |
                    FW_BOOT_FAULT_X | FW_BOOT_FAULT_Y;
    for (uint8_t a = 0; a < axes; ++a) fw_axis_init(&AX[a], &D, a, 2000000);
    fw_hal_set_start_cb(on_start);
    for (uint8_t a = 0; a < axes; ++a) fw_axis_poll(&AX[a]);
}

/* ---- Modbus helpers ---- */
static uint8_t RESP[300];
static int wr(unsigned addr, const uint16_t *v, unsigned n)
{
    uint8_t q[260]; q[0] = 16; q[1] = (uint8_t)(addr >> 8); q[2] = (uint8_t)addr; q[3] = 0; q[4] = (uint8_t)n; q[5] = (uint8_t)(2 * n);
    for (unsigned i = 0; i < n; ++i) { q[6 + 2 * i] = (uint8_t)(v[i] >> 8); q[7 + 2 * i] = (uint8_t)v[i]; }
    fw_mb_process(&D, q, 6 + 2 * n, RESP);
    return (RESP[0] & 0x80) ? RESP[1] : 0;
}
static int wr1(unsigned addr, uint16_t v) { return wr(addr, &v, 1); }
static int wr32(unsigned addr, uint32_t v) { const uint16_t w[2] = {(uint16_t)v, (uint16_t)(v >> 16)}; return wr(addr, w, 2); }
static int rd(unsigned addr, unsigned n)
{
    uint8_t q[5] = {3, (uint8_t)(addr >> 8), (uint8_t)addr, 0, (uint8_t)n};
    fw_mb_process(&D, q, 5, RESP);
    return (RESP[0] & 0x80) ? RESP[1] : 0;
}
static uint16_t reg(unsigned addr) { CHECK_EQ(rd(addr, 1), 0); return (uint16_t)((RESP[2] << 8) | RESP[3]); }
static uint32_t reg32(unsigned addr) { CHECK_EQ(rd(addr, 2), 0); return ((uint32_t)((RESP[4] << 8) | RESP[5]) << 16) | (uint32_t)((RESP[2] << 8) | RESP[3]); }
#define BX 0x1000u
#define BY 0x2000u
static void par(unsigned base, unsigned off, uint32_t v, bool w32) { CHECK_EQ(w32 ? wr32(base + 0x100 + off, v) : wr1(base + 0x100 + off, (uint16_t)v), 0); }
static void set_units(unsigned base, uint16_t mode, uint32_t ppr, uint32_t lead, uint16_t gn, uint16_t gd)
{
    par(base, FW_P_UNIT_MODE, mode, false); par(base, FW_P_PULSES_PER_REV, ppr, true); par(base, FW_P_LEAD_UM, lead, true);
    par(base, FW_P_GEAR_NUM, gn, false); par(base, FW_P_GEAR_DEN, gd, false);
}
static void run_until_done(int ax, uint32_t step)
{
    for (int g = 0; g < 100000 && fw_axis_running(&AX[ax]); ++g) { fake_pulses((uint8_t)ax, step); fw_axis_poll(&AX[ax]); }
}

/* a parameter image with the given unit parameters, for the pure functions */
static uint16_t P[FW_PAR_WORDS];
static void mkpar(uint16_t mode, uint32_t ppr, uint32_t lead, uint16_t gn, uint16_t gd)
{
    fw_cfg_t c; fw_cfg_defaults(&c); memcpy(P, c.par[0], sizeof(P));
    P[FW_P_UNIT_MODE] = mode; fw_set32(P, FW_P_PULSES_PER_REV, ppr); fw_set32(P, FW_P_LEAD_UM, lead); P[FW_P_GEAR_NUM] = gn; P[FW_P_GEAR_DEN] = gd;
}

/* ---------------------------------------------------------------- FR-029 conversion (pure) */
static void test_unit_basic_and_rounding(void)
{
    int32_t p; uint32_t hz;
    mkpar(FW_UM_MM, 10000, 10000, 1, 1);                     /* 1 um = 1 pulse */
    CHECK(fw_unit_to_pulses(P, 1000, &p) && p == 1000);
    CHECK(fw_unit_to_pulses(P, -1000, &p) && p == -1000);
    CHECK(fw_unit_to_pulses(P, 0, &p) && p == 0);
    mkpar(FW_UM_MM, 3, 2, 1, 1);                             /* 1.5 pulse / um: half rounds away from zero */
    CHECK(fw_unit_to_pulses(P, 1, &p) && p == 2);
    CHECK(fw_unit_to_pulses(P, -1, &p) && p == -2);
    CHECK(fw_unit_to_pulses(P, 3, &p) && p == 5);            /* 4.5 -> 5 */
    mkpar(FW_UM_MM, 10000, 5000, 3, 2);                      /* 10000 * 1.5 / 5000 = 3 pulse/um */
    CHECK(fw_unit_to_pulses(P, 7, &p) && p == 21);
    CHECK(fw_unit_speed_to_hz(P, 100, &hz) && hz == 300);
    mkpar(FW_UM_DEG, 10000, 10000, 1, 1);                    /* 360000 mdeg = 10000 pulses */
    CHECK(fw_unit_to_pulses(P, 360000, &p) && p == 10000);
    CHECK(fw_unit_to_pulses(P, 90000, &p) && p == 2500);
    CHECK(fw_unit_to_pulses(P, -180000, &p) && p == -5000);
    CHECK(fw_pulses_to_unit(P, 2500, &p) && p == 90000);
    mkpar(FW_UM_DEG, 10000, 10000, 10, 1);                   /* 10:1 reduction: 3600 deg of motor per load rev */
    CHECK(fw_unit_to_pulses(P, 360000, &p) && p == 100000);
    mkpar(FW_UM_PULSE, 1, 1, 1, 1);                          /* pulse mode: identity */
    CHECK(fw_unit_to_pulses(P, 12345, &p) && p == 12345);
    CHECK(fw_pulses_to_unit(P, -777, &p) && p == -777);
    CHECK(fw_unit_speed_to_hz(P, 5000, &hz) && hz == 5000);
}

static void test_unit_boundaries_and_overflow(void)
{
    int32_t p; uint32_t hz;
    mkpar(FW_UM_MM, 10000, 10000, 1, 1);
    CHECK(fw_unit_to_pulses(P, INT32_MAX, &p) && p == INT32_MAX);              /* S32 boundary */
    CHECK(fw_unit_to_pulses(P, INT32_MIN, &p) && p == INT32_MIN);
    CHECK(!fw_unit_to_pulses(P, (int64_t)INT32_MAX + 1, &p));                 /* one past */
    CHECK(!fw_unit_to_pulses(P, (int64_t)INT32_MIN - 1, &p));
    mkpar(FW_UM_MM, 10000, 5000, 1, 1);                                        /* 2 pulse/um */
    CHECK(!fw_unit_to_pulses(P, INT32_MAX, &p));
    CHECK(fw_unit_to_pulses(P, 1073741823, &p) && p == 2147483646);
    CHECK(!fw_unit_to_pulses(P, 1073741824, &p));                              /* 2^31 pulses */
    CHECK(fw_unit_to_pulses(P, -1073741824, &p) && p == INT32_MIN);            /* exactly -2^31 fits */
    /* worst legal parameter set: 1e7 ppr, gear 1000/1, lead 1 um: a 64-bit product would overflow (2^31*1e10) */
    mkpar(FW_UM_MM, 10000000, 1, 1000, 1);
    CHECK(!fw_unit_to_pulses(P, INT32_MAX, &p));
    CHECK(!fw_unit_to_pulses(P, 1, &p));                                       /* 1e10 pulses/um: 1 um alone overflows S32 */
    CHECK(fw_unit_to_pulses(P, 0, &p) && p == 0);
    CHECK(!fw_unit_speed_to_hz(P, UINT32_MAX, &hz));                           /* U32 overflow */
    mkpar(FW_UM_MM, 1, 10000000, 1, 1000);                                     /* finest: 1e-10 pulse/um */
    CHECK(fw_unit_to_pulses(P, INT32_MAX, &p) && p == 0);                      /* rounds to 0, no overflow */
    CHECK(!fw_pulses_to_unit(P, INT32_MAX, &p));                               /* 2.1e9 * 1e10 units */
    CHECK_EQ(fw_pulses_to_unit_sat(P, 5), INT32_MAX);
    CHECK_EQ(fw_pulses_to_unit_sat(P, -5), INT32_MIN);
    CHECK_EQ(fw_pulses_to_unit_sat(P, 0), 0);
    /* illegal parameters never divide by zero */
    mkpar(FW_UM_MM, 10000, 0, 1, 1); CHECK(!fw_unit_to_pulses(P, 5, &p));
    mkpar(FW_UM_MM, 10000, 10, 1, 0); CHECK(!fw_unit_to_pulses(P, 5, &p));
    mkpar(3, 10000, 10, 1, 1); CHECK(!fw_unit_to_pulses(P, 5, &p));
}

static void test_unit_roundtrip(void)
{
    static const struct { uint16_t m; uint32_t ppr, lead; uint16_t gn, gd; } S[] = {
        {FW_UM_MM, 10000, 10000, 1, 1}, {FW_UM_MM, 10000, 20000, 1, 1}, {FW_UM_MM, 3000, 7000, 3, 2},
        {FW_UM_MM, 10000, 5000, 3, 2}, {FW_UM_DEG, 10000, 1, 1, 1}, {FW_UM_DEG, 3333, 1, 7, 5}, {FW_UM_DEG, 10000, 1, 10, 1},
        {FW_UM_MM, 4096, 10000, 1, 100}, {FW_UM_MM, 1, 1000, 1, 1}, {FW_UM_MM, 10000000, 10000000, 1000, 1}};
    long worst_p = 0, worst_u = 0;
    for (unsigned s = 0; s < sizeof(S) / sizeof(S[0]); ++s) {
        mkpar(S[s].m, S[s].ppr, S[s].lead, S[s].gn, S[s].gd);
        const uint64_t num = (uint64_t)S[s].ppr * S[s].gn;
        const uint64_t den = (uint64_t)(S[s].m == FW_UM_MM ? S[s].lead : 360000u) * S[s].gd;
        for (int64_t v = -3000000; v <= 3000000; v += 1237) {
            int32_t p, u, p2;
            /* unit -> pulses -> unit: exact within 1 unit when a pulse is at most 1 unit (num >= den ... coarser steps) */
            if (fw_unit_to_pulses(P, v, &p) && num >= den) {
                CHECK(fw_pulses_to_unit(P, p, &u));
                long e = labs((long)(u - v)); if (e > worst_u) worst_u = e;
                CHECK(e <= 1);
            }
            /* pulses -> unit -> pulses: within 1 pulse when one unit is at most one pulse (num <= den) */
            if (fw_pulses_to_unit(P, v, &u) && num <= den) {
                CHECK(fw_unit_to_pulses(P, u, &p2));
                long e = labs((long)(p2 - v)); if (e > worst_p) worst_p = e;
                CHECK(e <= 1);
            }
            /* direct conversion error vs exact rational value: <= 0.5 pulse (checked in doubles, values are small) */
            if (fw_unit_to_pulses(P, v, &p)) {
                const double exact = (double)v * (double)num / (double)den;
                CHECK(fabs((double)p - exact) <= 0.5000001);
            }
        }
    }
    printf("  worst round-trip error: %ld pulse, %ld unit\n", worst_p, worst_u);
}

/* ---------------------------------------------------------------- FR-029 commands (register path + axis) */
static void test_unit_commands(void)
{
    setup(2, false);
    set_units(BX, FW_UM_MM, 10000, 10000, 1, 1);                           /* 1 um = 1 pulse */
    CHECK_EQ(wr32(BX + 0x4A, 5000), 0); CHECK_EQ(wr32(BX + 0x4C, 20000), 0);          /* 5 mm at 20 mm/s -> 5000 pulses, 20 kHz */
    CHECK_EQ(wr1(BX + 0x40, FW_CMD_START_REL_UNIT), 0);
    CHECK_EQ(reg(BX + 0x0B), FW_RES_OK);
    CHECK(FP[0].active); CHECK_EQ(FP[0].target, 5000);
    run_until_done(0, 41);
    CHECK_EQ(D.ax[0].leg_result, 1);
    fw_axis_poll(&AX[0]);
    CHECK_EQ(reg32(BX + 0x30), 5000);                                      /* position_unit */
    /* absolute to -2 mm: 7000 pulses backwards */
    CHECK_EQ(wr32(BX + 0x4A, (uint32_t)-2000), 0);
    AX[0].pos_valid = true;
    CHECK_EQ(wr1(BX + 0x40, FW_CMD_START_ABS_UNIT), 0);
    CHECK_EQ(reg(BX + 0x0B), FW_RES_OK);
    CHECK_EQ(FP[0].target, 7000); CHECK(!FP[0].dir_high);
    run_until_done(0, 100);
    fw_axis_poll(&AX[0]);
    CHECK_EQ(reg32(BX + 0x30), (uint32_t)-2000);
    /* speed over max_speed (default 100000): 150 mm/s = 150000 Hz -> 0x17, nothing started */
    const uint32_t starts = FP[0].starts;
    CHECK_EQ(wr32(BX + 0x4C, 150000), 0); CHECK_EQ(wr32(BX + 0x4A, 100), 0);
    CHECK_EQ(wr1(BX + 0x40, FW_CMD_START_REL_UNIT), 0);
    CHECK_EQ(reg(BX + 0x0B), FW_RES_RANGE); CHECK_EQ(FP[0].starts, starts);
    CHECK_EQ(wr32(BX + 0x4C, 99999999), 0);                                  /* over 1 MHz too */
    CHECK_EQ(wr1(BX + 0x40, FW_CMD_START_REL_UNIT), 0); CHECK_EQ(reg(BX + 0x0B), FW_RES_RANGE);
    CHECK_EQ(wr32(BX + 0x4C, 50), 0);                                        /* below 100 Hz */
    CHECK_EQ(wr1(BX + 0x40, FW_CMD_START_REL_UNIT), 0); CHECK_EQ(reg(BX + 0x0B), FW_RES_RANGE);
    /* target overflow: 2 pulse per um, 2^30 um -> 0x17 */
    set_units(BX, FW_UM_MM, 10000, 5000, 1, 1);
    CHECK_EQ(wr32(BX + 0x4C, 20000), 0); CHECK_EQ(wr32(BX + 0x4A, 1073741824), 0);
    CHECK_EQ(wr1(BX + 0x40, FW_CMD_START_REL_UNIT), 0); CHECK_EQ(reg(BX + 0x0B), FW_RES_RANGE);
    CHECK_EQ(FP[0].starts, starts);
    /* pulse mode: unit commands refused */
    set_units(BX, FW_UM_PULSE, 10000, 5000, 1, 1);
    CHECK_EQ(wr32(BX + 0x4A, 100), 0);
    CHECK_EQ(wr1(BX + 0x40, FW_CMD_START_REL_UNIT), 0); CHECK_EQ(reg(BX + 0x0B), FW_RES_RANGE);
    /* parameter range checks (exception 03, nothing changes) */
    CHECK_EQ(wr1(BX + 0x140, 3), 3); CHECK_EQ(wr32(BX + 0x141, 0), 3); CHECK_EQ(wr32(BX + 0x141, 10000001), 3);
    CHECK_EQ(wr32(BX + 0x143, 0), 3); CHECK_EQ(wr1(BX + 0x145, 0), 3); CHECK_EQ(wr1(BX + 0x146, 1001), 3);
    CHECK_EQ(wr1(BX + 0x147, 0), 3); CHECK_EQ(wr1(BX + 0x147, 301), 3); CHECK_EQ(wr1(BX + 0x147, 300), 0);
    CHECK_EQ(wr1(BX + 0x04E, 8), 3);
    CHECK_EQ(reg(BX + 0x140), FW_UM_PULSE);
    /* defaults (5.12) */
    setup(2, false);
    CHECK_EQ(reg(BX + 0x140), 0); CHECK_EQ(reg32(BX + 0x141), 10000); CHECK_EQ(reg32(BX + 0x143), 10000);
    CHECK_EQ(reg(BX + 0x145), 1); CHECK_EQ(reg(BX + 0x146), 1); CHECK_EQ(reg(BX + 0x147), 60);
    /* axis 2 is independent */
    set_units(BY, FW_UM_DEG, 10000, 1, 1, 1);
    CHECK_EQ(reg(BX + 0x140), 0); CHECK_EQ(reg(BY + 0x140), FW_UM_DEG);
}

/* ---------------------------------------------------------------- FR-030 */
static void test_setup_done_and_max_rpm(void)
{
    setup(2, false);
    CHECK_EQ(reg(0x0130), 0);
    CHECK_EQ(wr1(0x0130, 1), 0); CHECK_EQ(reg(0x0130), 1); CHECK_EQ(CFG_CHANGED, 1);       /* persisted change -> save timer */
    CHECK_EQ(wr1(0x0130, 1), 0); CHECK_EQ(CFG_CHANGED, 1);                                 /* same value: no new save */
    CHECK_EQ(wr1(0x0130, 2), 3); CHECK_EQ(reg(0x0130), 1);
    CHECK_EQ(D.cfg.setup_done, 1);
    fw_cfg_seal(&D.cfg); CHECK_EQ(fw_cfg_verify(&D.cfg, sizeof(D.cfg)), FW_CFG_OK);       /* part of the stored image */
    D.cfg.setup_done = 2; fw_cfg_seal(&D.cfg); CHECK_EQ(fw_cfg_verify(&D.cfg, sizeof(D.cfg)), FW_CFG_BAD_RANGE);
    /* max_rpm_calc: min(1 MHz, max_speed) * 60 / ppr; default max_speed 100000, ppr 10000 -> 600 rpm */
    setup(2, false);
    CHECK_EQ(reg(BX + 0x32), 600);
    par(BX, FW_P_MAX_SPEED, 1000000, true); CHECK_EQ(reg(BX + 0x32), 6000);
    par(BX, FW_P_PULSES_PER_REV, 1000, true); CHECK_EQ(reg(BX + 0x32), 60000);
    par(BX, FW_P_PULSES_PER_REV, 1, true); CHECK_EQ(reg(BX + 0x32), 65535);                 /* clamp */
    par(BX, FW_P_PULSES_PER_REV, 10000000, true); CHECK_EQ(reg(BX + 0x32), 6);              /* 1e6*60/1e7 = 6 rpm */
    mkpar(FW_UM_MM, 10000000, 1, 1, 1); fw_set32(P, FW_P_MAX_SPEED, 100); CHECK_EQ(fw_max_rpm_calc(P), 0);       /* 0.0006 rpm */
    par(BX, FW_P_PULSES_PER_REV, 3, true);
    CHECK_EQ(reg(BX + 0x32), 65535);
    CHECK_EQ(reg(BY + 0x32), 600);                                                            /* per axis */
}

/* ---------------------------------------------------------------- FR-031 */
static void test_wiring_status(void)
{
    setup(2, false);
    D.now_ms = 1000;
    /* idle: STOP inputs closed (di bit 0), START idle, nothing counted, DO off, link down */
    D.di_raw = 0; D.do_state = 0; D.link = false;
    uint16_t w = reg(0x0033);
    CHECK_EQ(w, FW_WIRING_VALID | 0x01 | 0x02 | 0x10 | 0x20);
    D.di_raw = 0x02;                                   /* X STOP open (wire cut / pressed) */
    CHECK_EQ(reg(0x0033) & 0x01, 0); CHECK(reg(0x0033) & 0x10);
    D.di_raw = 0x20;                                   /* Y STOP open */
    CHECK(reg(0x0033) & 0x01); CHECK_EQ(reg(0x0033) & 0x10, 0);
    D.di_raw = 0x01;                                   /* X START pressed: not waiting */
    CHECK_EQ(reg(0x0033) & 0x02, 0); CHECK(reg(0x0033) & 0x20);
    D.di_raw = 0x10;                                   /* Y START pressed */
    CHECK_EQ(reg(0x0033) & 0x20, 0); CHECK(reg(0x0033) & 0x02);
    D.di_raw = 0;
    D.do_state = 0x1; CHECK(reg(0x0033) & 0x08); CHECK_EQ(reg(0x0033) & 0x80, 0);
    D.do_state = 0x2; CHECK_EQ(reg(0x0033) & 0x08, 0); CHECK(reg(0x0033) & 0x80);
    D.do_state = 0x4; CHECK_EQ(reg(0x0033) & 0x88, 0);                                    /* DO3 is not a servo output */
    D.do_state = 0;
    D.link = true; CHECK(reg(0x0033) & 0x100); D.link = false; CHECK_EQ(reg(0x0033) & 0x100, 0);
    /* loopback: PCNT counts the pulses of a real move (b2), mismatch clears it */
    CHECK_EQ(reg(0x0033) & 0x04, 0);
    wr32(BX + 0x42, 1000); wr1(BX + 0x40, FW_CMD_START_REL);
    fake_pulses(0, 100); fw_axis_poll(&AX[0]);
    CHECK(reg(0x0033) & 0x04); CHECK_EQ(reg(0x0033) & 0x40, 0);                           /* Y has no count */
    run_until_done(0, 50);
    CHECK(reg(0x0033) & 0x04);
    /* RTU frames (b9): only in RTU mode and only for 3 s after the last valid frame */
    D.comm_mode_active = FW_MODE_RTU; D.now_ms = 5000;
    CHECK_EQ(reg(0x0033) & 0x200, 0);
    fw_dev_note_rtu_rx(&D); CHECK(reg(0x0033) & 0x200);
    D.now_ms = 8000; CHECK(reg(0x0033) & 0x200);
    D.now_ms = 8001; CHECK_EQ(reg(0x0033) & 0x200, 0);
    D.comm_mode_active = FW_MODE_TCP; D.now_ms = 5000; fw_dev_note_rtu_rx(&D); CHECK_EQ(reg(0x0033) & 0x200, 0);
    /* I/O not up: the DI-derived bits are not claimed, valid bit off */
    D.boot_status &= (uint16_t)~FW_BOOT_STOP_IO;
    w = reg(0x0033);
    CHECK_EQ(w & (FW_WIRING_VALID | 0xFF), 0);
    /* 1-axis model: Y bits stay 0 */
    setup(1, false); D.di_raw = 0;
    CHECK_EQ(reg(0x0033) & 0xF0, 0); CHECK_EQ(reg(0x0033) & 0x0F, 0x03);
    /* read only */
    CHECK_EQ(wr1(0x0033, 1), 2); CHECK_EQ(wr1(0x0034, 1), 2);
}

/* ---------------------------------------------------------------- FR-033 */
static void test_alarm_hint(void)
{
    setup(2, false);
    CHECK_EQ(reg(0x0034), 0);
    D.ax[0].alarm_code = FW_AL_COMMLOSS;
    CHECK_EQ(reg(0x0034), FW_AL_COMMLOSS);
    D.ax[1].alarm_code = FW_AL_ESTOP;                       /* highest priority wins, whichever axis */
    CHECK_EQ(reg(0x0034), FW_AL_ESTOP);
    D.ax[0].alarm_code = FW_AL_PE_HW; CHECK_EQ(reg(0x0034), FW_AL_ESTOP);
    D.ax[1].alarm_code = FW_AL_NONE; CHECK_EQ(reg(0x0034), FW_AL_PE_HW);
    D.ax[1].alarm_code = FW_AL_WDT; CHECK_EQ(reg(0x0034), FW_AL_PE_HW);
    D.ax[0].alarm_code = FW_AL_PE_FAULTPATH; D.ax[1].alarm_code = FW_AL_PARAM; CHECK_EQ(reg(0x0034), FW_AL_PE_FAULTPATH);
    D.ax[0].alarm_code = FW_AL_NVM; D.ax[1].alarm_code = FW_AL_PARAM; CHECK_EQ(reg(0x0034), FW_AL_PARAM);
    D.ax[0].alarm_code = FW_AL_NONE; D.ax[1].alarm_code = FW_AL_NONE; CHECK_EQ(reg(0x0034), 0);
    /* live: STOP input makes the axis alarm 0x0101 */
    FAKE_STOP_IN[0] = true; fake_stop_isr(0); fw_axis_poll(&AX[0]);
    CHECK_EQ(reg(0x0034), FW_AL_ESTOP);
}

/* ---------------------------------------------------------------- FR-032 */
static void test_test_hold(void)
{
    setup(2, false);
    /* 60 rpm at 10000 pulses/rev = 10 kHz */
    CHECK_EQ(wr1(BX + 0x40, FW_CMD_TEST_POS), 0);
    CHECK_EQ(reg(BX + 0x0B), FW_RES_OK);
    CHECK(FP[0].active); CHECK(FP[0].dir_high);
    CHECK_RANGE(FP[0].first_hz, 100, 10000);
    fake_pulses(0, 20000); fw_axis_poll(&AX[0]);
    CHECK_EQ(FP[0].freq, 10000);
    CHECK_EQ(D.ax[0].cmd_speed, 10000);
    /* keepalive every 400 ms keeps it going */
    for (int i = 0; i < 5; ++i) {
        FAKE_NOW += 400; CHECK_EQ(wr1(BX + 0x40, FW_CMD_TEST_POS), 0); CHECK_EQ(reg(BX + 0x0B), FW_RES_OK);
        fw_axis_poll(&AX[0]); CHECK(AX[0].mv == MV_RUN);
    }
    /* exactly 500 ms after the last keepalive: still running; 501: decelerating */
    FAKE_NOW += 500; fw_axis_poll(&AX[0]); CHECK(AX[0].mv == MV_RUN);
    FAKE_NOW += 1; fw_axis_poll(&AX[0]); CHECK(AX[0].mv == MV_DECEL);
    CHECK_EQ(D.ax[0].state, FW_AS_DECEL_STOP);
    /* too late for a keepalive: the decel stop is not cancelled */
    CHECK_EQ(wr1(BX + 0x40, FW_CMD_TEST_POS), 0); CHECK_EQ(reg(BX + 0x0B), FW_RES_RUNNING);
    CHECK(AX[0].mv == MV_DECEL);
    run_until_done(0, 10);
    CHECK(!fw_axis_running(&AX[0])); CHECK(!AX[0].test_active);
    CHECK_EQ(D.ax[0].last_stop_cause, FW_SC_STOP_CMD);
    CHECK(!FAKE_SERVO[0]);
    CHECK(AX[0].position > 20000);                              /* the pulses really sent are booked */
    /* negative direction, opposite command while running is refused, STOP command ends it */
    const int32_t p0 = AX[0].position;
    FAKE_NOW += 10;
    CHECK_EQ(wr1(BX + 0x40, FW_CMD_TEST_NEG), 0); CHECK_EQ(reg(BX + 0x0B), FW_RES_OK); CHECK(!FP[0].dir_high);
    CHECK_EQ(wr1(BX + 0x40, FW_CMD_TEST_POS), 0); CHECK_EQ(reg(BX + 0x0B), FW_RES_RUNNING);
    fake_pulses(0, 3000); fw_axis_poll(&AX[0]);
    CHECK_EQ(wr1(BX + 0x40, FW_CMD_STOP), 0);
    CHECK(AX[0].mv == MV_DECEL);
    run_until_done(0, 10);
    CHECK(AX[0].position < p0);
    /* STOP input active: refused with 0x10, also as keepalive */
    FAKE_NOW += 10;
    FAKE_STOP_IN[1] = true;
    CHECK_EQ(wr1(BY + 0x40, FW_CMD_TEST_POS), 0); CHECK_EQ(reg(BY + 0x0B), FW_RES_ESTOP); CHECK(!FP[1].active);
    FAKE_STOP_IN[1] = false;
    CHECK_EQ(wr1(BY + 0x40, FW_CMD_TEST_POS), 0); CHECK_EQ(reg(BY + 0x0B), FW_RES_OK); CHECK(FP[1].active);
    FAKE_STOP_IN[1] = true; fake_stop_isr(1);
    CHECK_EQ(wr1(BY + 0x40, FW_CMD_TEST_POS), 0); CHECK_EQ(reg(BY + 0x0B), FW_RES_ESTOP);
    fw_axis_poll(&AX[1]); CHECK(!fw_axis_running(&AX[1])); CHECK(!AX[1].test_active);          /* STOP while running cuts it */
    /* speed range: 300 rpm * 10000 / 60 = 50 kHz ok; 300 rpm * 20000 / 60 = 100 kHz ok (= max_speed); 300 rpm * 20001 over */
    setup(2, false);
    wr1(BX + 0x147, 300); par(BX, FW_P_PULSES_PER_REV, 20001, true);
    CHECK_EQ(wr1(BX + 0x40, FW_CMD_TEST_POS), 0); CHECK_EQ(reg(BX + 0x0B), FW_RES_RANGE); CHECK(!FP[0].active);
    par(BX, FW_P_PULSES_PER_REV, 20000, true);
    CHECK_EQ(wr1(BX + 0x40, FW_CMD_TEST_POS), 0); CHECK_EQ(reg(BX + 0x0B), FW_RES_OK);
    fw_axis_command(&AX[0], FW_CMD_QUICK_STOP);
    par(BX, FW_P_PULSES_PER_REV, 100, true); wr1(BX + 0x147, 1);                  /* 1.67 Hz: below 100 Hz */
    CHECK_EQ(wr1(BX + 0x40, FW_CMD_TEST_NEG), 0); CHECK_EQ(reg(BX + 0x0B), FW_RES_RANGE);
}

/* ---------------------------------------------------------------- FR-034 */
static void test_points(void)
{
    setup(2, false);
    set_units(BX, FW_UM_MM, 10000, 10000, 1, 1);
    const unsigned P0 = 0x6000, P1 = 0x6000 + 0x10, PY = 0x6000 + 0x80 + 0x20;
    CHECK_EQ(reg(P0 + 6), 0);                                                             /* default: empty */
    /* write point 1 of X in one FC16, read back */
    const uint16_t v[7] = {0x4E20, 0x0000, 0x1F40, 0x0000, 200, 300, 1};                  /* 20000 um, 8000 um/s, 200/300 ms */
    CHECK_EQ(wr(P1, v, 7), 0);
    CHECK_EQ(reg32(P1), 20000); CHECK_EQ(reg32(P1 + 2), 8000); CHECK_EQ(reg(P1 + 4), 200); CHECK_EQ(reg(P1 + 5), 300); CHECK_EQ(reg(P1 + 6), 1);
    CHECK(CFG_CHANGED >= 1);
    CHECK_EQ(reg(P0 + 6), 0); CHECK_EQ(reg(PY + 6), 0);                                  /* neighbours untouched */
    /* validation: atomic, nothing changes */
    const int cc = CFG_CHANGED;
    uint16_t bad[7] = {1, 0, 0, 0, 200, 300, 1};  CHECK_EQ(wr(P1, bad, 7), 3);                    /* speed 0 with valid=1 */
    memcpy(bad, v, sizeof(bad)); bad[4] = 9;       CHECK_EQ(wr(P1, bad, 7), 3);                    /* accel < 10 */
    memcpy(bad, v, sizeof(bad)); bad[5] = 0;       CHECK_EQ(wr(P1, bad, 7), 3);
    memcpy(bad, v, sizeof(bad)); bad[6] = 2;       CHECK_EQ(wr(P1, bad, 7), 3);
    CHECK_EQ(wr1(P1 + 5, 60001), 3);
    CHECK_EQ(wr1(P1 + 7, 0), 2);                                                          /* reserved word */
    CHECK_EQ(wr1(P1 + 1, 0), 3);                                                          /* half of a 32-bit item */
    CHECK_EQ(reg32(P1), 20000); CHECK_EQ(CFG_CHANGED, cc);
    CHECK_EQ(rd(0x6000 + 0x100, 1), 2);                                                   /* beyond the table */
    /* POINT_GO: absolute move to 20 mm at 8 mm/s with the stored accel/decel */
    AX[0].pos_valid = true;
    CHECK_EQ(wr1(BX + 0x4E, 1), 0);
    CHECK_EQ(wr1(BX + 0x40, FW_CMD_POINT_GO), 0); CHECK_EQ(reg(BX + 0x0B), FW_RES_OK);
    CHECK_EQ(FP[0].target, 20000); CHECK(FP[0].dir_high);
    CHECK_EQ(AX[0].prof.target_speed, 8000); CHECK_EQ(AX[0].prof.accel_pulses, 1300);        /* stored accel 200 ms */
    CHECK_EQ(AX[0].prof.decel_start, 20000 - 1950);                                         /* stored decel 300 ms */
    run_until_done(0, 77);
    fw_axis_poll(&AX[0]);
    CHECK_EQ(reg32(BX + 0x30), 20000);
    /* GO to an empty point or an unknown index */
    CHECK_EQ(wr1(BX + 0x4E, 2), 0);
    CHECK_EQ(wr1(BX + 0x40, FW_CMD_POINT_GO), 0); CHECK_EQ(reg(BX + 0x0B), FW_RES_RANGE);
    /* GO with a stored speed beyond max_speed (set the limit lower afterwards) */
    par(BX, FW_P_GEAR_NUM, 100, false);                                          /* 100x pulses per um: 800 kHz > max_speed */
    CHECK_EQ(wr1(BX + 0x4E, 1), 0);
    CHECK_EQ(wr1(BX + 0x40, FW_CMD_POINT_GO), 0); CHECK_EQ(reg(BX + 0x0B), FW_RES_RANGE);
    par(BX, FW_P_GEAR_NUM, 1, false);
    /* TEACH point 3 at the current position (20000 pulses = 20000 um) with the current operating speed/accel */
    CHECK_EQ(wr32(BX + 0x44, 8000), 0); wr1(BX + 0x46, 150); wr1(BX + 0x47, 250);
    CHECK_EQ(wr1(BX + 0x4E, 3), 0);
    CHECK_EQ(wr1(BX + 0x40, FW_CMD_POINT_TEACH), 0); CHECK_EQ(reg(BX + 0x0B), FW_RES_OK);
    CHECK_EQ(reg32(0x6000 + 0x30), 20000); CHECK_EQ(reg32(0x6000 + 0x32), 8000);
    CHECK_EQ(reg(0x6000 + 0x34), 150); CHECK_EQ(reg(0x6000 + 0x35), 250); CHECK_EQ(reg(0x6000 + 0x36), 1);
    /* teach refused: position unknown / running */
    AX[0].pos_valid = false;
    CHECK_EQ(wr1(BX + 0x40, FW_CMD_POINT_TEACH), 0); CHECK_EQ(reg(BX + 0x0B), FW_RES_POS_INVALID);
    AX[0].pos_valid = true;
    CHECK_EQ(wr32(BX + 0x42, 1000), 0); wr1(BX + 0x40, FW_CMD_START_REL);
    CHECK_EQ(wr1(BX + 0x40, FW_CMD_POINT_TEACH), 0); CHECK_EQ(reg(BX + 0x0B), FW_RES_RUNNING);
    CHECK_EQ(wr1(BX + 0x40, FW_CMD_POINT_GO), 0); CHECK_EQ(reg(BX + 0x0B), FW_RES_RUNNING);
    run_until_done(0, 100);
    /* points are saved: part of the stored image and survive a store round trip; Y is separate */
    fw_cfg_seal(&D.cfg);
    fw_cfg_t img = D.cfg; CHECK_EQ(fw_cfg_verify(&img, sizeof(img)), FW_CFG_OK);
    CHECK_EQ(fw_w32(img.pt[0][1], FW_PT_POS), 20000); CHECK_EQ(img.pt[0][3][FW_PT_VALID], 1); CHECK_EQ(img.pt[1][3][FW_PT_VALID], 0);
    img.pt[1][2][FW_PT_VALID] = 1; fw_cfg_seal(&img);
    CHECK_EQ(fw_cfg_verify(&img, sizeof(img)), FW_CFG_BAD_RANGE);                         /* valid point with speed 0 is refused */
    /* Y axis table and 1-axis model */
    const uint16_t vy[7] = {100, 0, 50, 0, 20, 20, 1};
    CHECK_EQ(wr(0x6080, vy, 7), 0); CHECK_EQ(reg32(0x6080), 100);
    setup(1, false);
    CHECK_EQ(rd(0x6080, 1), 2); CHECK_EQ(wr(0x6080, vy, 7), 2); CHECK_EQ(rd(0x6000, 1), 0);
}

/* ---------------------------------------------------------------- FR-035 */
static void test_resets(void)
{
    setup(2, true);                                   /* RTU model */
    wr1(0x0130, 1);
    /* locked: refused 0x11, nothing changes */
    CHECK_EQ(wr1(0x4006, 5), 3);
    CHECK_EQ(wr1(0x0100, FW_SYS_NET_RESET), 0); CHECK_EQ(reg(0x0101), FW_SRES_LOCKED);
    CHECK_EQ(wr1(0x0100, FW_SYS_FACTORY_RESET), 0); CHECK_EQ(reg(0x0101), FW_SRES_LOCKED);
    CHECK_EQ(reg(0x0130), 1);
    /* unlocked + custom network and a point */
    D.now_ms = 1000; CHECK_EQ(wr1(0x0102, FW_UNLOCK_KEY), 0);
    CHECK_EQ(wr32(0x4000, 0xC0A80105), 0); CHECK_EQ(wr1(0x4006, 7), 0);
    const uint16_t v[7] = {1, 0, 5, 0, 20, 20, 1}; CHECK_EQ(wr(0x6000, v, 7), 0);
    CHECK_EQ(wr1(BX + 0x140, 1), 0);
    /* unlock times out after 10 s */
    D.now_ms = 1000 + FW_UNLOCK_MS; CHECK_EQ(wr1(0x0100, FW_SYS_NET_RESET), 0); CHECK_EQ(reg(0x0101), FW_SRES_LOCKED);
    D.now_ms = 2000; CHECK_EQ(wr1(0x0102, FW_UNLOCK_KEY), 0);
    /* axis running: refused 0x12 */
    wr32(BX + 0x42, 1000); wr1(BX + 0x40, FW_CMD_START_REL); CHECK(fw_axis_running(&AX[0]));
    CHECK_EQ(wr1(0x0100, FW_SYS_NET_RESET), 0); CHECK_EQ(reg(0x0101), FW_SRES_AXIS_RUNNING);
    CHECK_EQ(reg32(0x4000), 0xC0A80105);
    run_until_done(0, 100);
    /* NET_RESET: only the network words go back; mode = RTU model default; others kept */
    CHECK_EQ(wr1(0x0100, FW_SYS_NET_RESET), 0); CHECK_EQ(reg(0x0101), FW_SRES_RUNNING_CMD);
    CHECK_EQ(reg32(0x4000), 0xC0A800DC); CHECK_EQ(reg(0x4006), 1); CHECK_EQ(reg(0x400E), FW_MODE_RTU);
    CHECK_EQ(reg(0x0130), 1); CHECK_EQ(reg(BX + 0x140), 1); CHECK_EQ(reg32(0x6000), 1);
    /* the unlock is consumed: a second reset needs a new unlock */
    CHECK_EQ(wr1(0x0100, FW_SYS_NET_RESET), 0); CHECK_EQ(reg(0x0101), FW_SRES_LOCKED);
    /* FACTORY_RESET: everything including setup_done, units and points */
    CHECK_EQ(wr1(0x0102, FW_UNLOCK_KEY), 0);
    CHECK_EQ(wr32(0x4000, 0xC0A80109), 0);
    CHECK_EQ(wr1(0x0100, FW_SYS_FACTORY_RESET), 0); CHECK_EQ(reg(0x0101), FW_SRES_RUNNING_CMD);
    CHECK_EQ(reg(0x0130), 0); CHECK_EQ(reg(BX + 0x140), 0); CHECK_EQ(reg32(0x6000), 0); CHECK_EQ(reg(0x6006), 0);
    CHECK_EQ(reg32(0x4000), 0xC0A800DC); CHECK_EQ(reg(0x400E), FW_MODE_RTU);
    /* TCP model default mode */
    setup(1, false); D.now_ms = 10; wr1(0x0102, FW_UNLOCK_KEY);
    CHECK_EQ(wr1(0x0100, FW_SYS_NET_RESET), 0); CHECK_EQ(reg(0x0101), FW_SRES_RUNNING_CMD); CHECK_EQ(reg(0x400E), FW_MODE_TCP);
    CHECK_EQ(wr1(0x0100, 9), 3);                                                          /* unknown sys command */
    /* direct function: other commands are not handled there */
    CHECK_EQ(fw_sys_reset_cmd(&D, FW_SYS_SAVE, false), FW_SRES_REFUSED);
}

static void test_boot_hold(void)
{
    fw_nrhold_t h;
    fw_nrhold_init(&h);
    CHECK(!fw_nrhold_update(&h, true, true, 100));                      /* boot sample: both active */
    CHECK(!fw_nrhold_update(&h, true, true, 5000));
    CHECK(!fw_nrhold_update(&h, true, true, 100 + FW_NETRESET_HOLD_MS - 1));
    CHECK(fw_nrhold_update(&h, true, true, 100 + FW_NETRESET_HOLD_MS));  /* exactly 10 s */
    CHECK(!fw_nrhold_update(&h, true, true, 100 + FW_NETRESET_HOLD_MS + 1));   /* fires once */
    /* released before 10 s cancels for good, even if pressed again */
    fw_nrhold_init(&h);
    CHECK(!fw_nrhold_update(&h, true, true, 0));
    CHECK(!fw_nrhold_update(&h, true, false, 3000));
    CHECK(!fw_nrhold_update(&h, true, true, 4000));
    CHECK(!fw_nrhold_update(&h, true, true, 20000));
    /* only one input at boot: never */
    fw_nrhold_init(&h);
    CHECK(!fw_nrhold_update(&h, true, false, 0)); CHECK(!fw_nrhold_update(&h, true, true, 1)); CHECK(!fw_nrhold_update(&h, true, true, 20000));
    fw_nrhold_init(&h);
    CHECK(!fw_nrhold_update(&h, false, true, 0)); CHECK(!fw_nrhold_update(&h, true, true, 20000));
    /* millisecond counter wrap */
    fw_nrhold_init(&h);
    CHECK(!fw_nrhold_update(&h, true, true, 0xFFFFFF00u));
    CHECK(!fw_nrhold_update(&h, true, true, 5000));
    CHECK(fw_nrhold_update(&h, true, true, 0xFFFFFF00u + FW_NETRESET_HOLD_MS));
    /* net reset helper keeps non-network settings */
    fw_cfg_t c; fw_cfg_defaults_model(&c, false);
    fw_set32(c.net, FW_N_IP, 0x0A000001); c.par[0][FW_P_UNIT_MODE] = 2; c.setup_done = 1; c.net[FW_N_COMM_MODE] = 1;
    fw_cfg_net_reset(&c, false);
    CHECK_EQ(fw_w32(c.net, FW_N_IP), 0xC0A800DC); CHECK_EQ(c.net[FW_N_COMM_MODE], FW_MODE_TCP);
    CHECK_EQ(c.par[0][FW_P_UNIT_MODE], 2); CHECK_EQ(c.setup_done, 1);
}

/* the existing X/Y behaviour must not change when the new registers are untouched */
static void test_regression_defaults(void)
{
    setup(2, false);
    CHECK_EQ(reg(0x0020 + 1), 2);                                   /* axis count */
    CHECK_EQ(reg32(BX + 0x44), 10000);                              /* speed default unchanged */
    CHECK_EQ(reg32(BX + 0x4A), 0); CHECK_EQ(reg32(BX + 0x4C), 0); CHECK_EQ(reg(BX + 0x4E), 0);
    CHECK_EQ(fw_cfg_verify(&D.cfg, sizeof(D.cfg)), FW_CFG_OK);
}

int main(void)
{
    RUN(test_unit_basic_and_rounding); RUN(test_unit_boundaries_and_overflow); RUN(test_unit_roundtrip);
    RUN(test_unit_commands); RUN(test_setup_done_and_max_rpm); RUN(test_wiring_status); RUN(test_alarm_hint);
    RUN(test_test_hold); RUN(test_points); RUN(test_resets); RUN(test_boot_hold); RUN(test_regression_defaults);
    SUMMARY();
}
