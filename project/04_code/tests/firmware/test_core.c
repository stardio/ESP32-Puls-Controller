/* Host tests of fw_core: profile maths, STOP/START state machines, parameter tables, mode selection, deferred save,
 * comm-loss monitor, CRC. Completion criteria of notes/firmware.md section 5 are referenced as [T-x]. */
#include "fw_test.h"
#include "fw_params.h"
#include "fw_profile.h"
#include "fw_stop.h"

/* [T-profile-1] the pulse period never gives MORE than the requested frequency; too-low requests are rejected */
static void test_ticks(void)
{
    uint32_t t = 0;
    const uint32_t res[] = {2000000u, 10000000u, 40000000u};
    for (unsigned r = 0; r < 3; ++r)
        for (uint32_t f = 100; f <= 1000000; f += 997) {
            if (!fw_ticks_for_freq(res[r], f, FW_MAX_PERIOD_TICKS, &t)) continue;
            CHECK(fw_freq_actual(res[r], t) <= f);
            CHECK(t >= 2);
        }
    CHECK(fw_ticks_for_freq(2000000, 1000000, 65535, &t) && t == 2);
    CHECK(fw_ticks_for_freq(2000000, 300000, 65535, &t) && t == 7 && fw_freq_actual(2000000, t) == 285714);
    CHECK(!fw_ticks_for_freq(2000000, 30, 65535, &t));                 /* needs 66667 ticks */
    CHECK(fw_ticks_for_freq(2000000, 31, 65535, &t));
    CHECK(!fw_ticks_for_freq(2000000, 0, 65535, &t));
    CHECK(!fw_ticks_for_freq(40000000, 100, 65535, &t));               /* 400000 ticks: 40 MHz cannot do 100 Hz */
    CHECK(fw_res_valid(2000000) && !fw_res_valid(3000000));
}

static void test_start_speed(void)
{
    CHECK_EQ(fw_start_speed(0, 10000), 5000);          /* automatic: min(5000, speed) (servo1axis) */
    CHECK_EQ(fw_start_speed(0, 3000), 3000);
    CHECK_EQ(fw_start_speed(0, 100), 100);
    CHECK_EQ(fw_start_speed(8000, 10000), 8000);       /* manual */
    CHECK_EQ(fw_start_speed(8000, 6000), 6000);        /* never above the move speed */
}

/* [T-profile-2] trapezoid numbers of the 1 MHz servo1axis run (CURRENT_STATE.md): 1,000,000 pulses, 5 kHz start, 500/500 ms */
static void test_trapezoid(void)
{
    fw_profile_t p;
    fw_profile_calc(&p, 1000000, 5000, 1000000, 500, 500);
    CHECK_RANGE(p.accel_pulses, 251000, 251500);
    CHECK_EQ(p.position - p.decel_start, p.accel_pulses);              /* symmetric ramps */
    CHECK_RANGE(p.decel_start - p.accel_pulses, 497000, 498000);       /* ~497,500 plateau pulses in the log */
    CHECK_EQ(fw_profile_freq(&p, 0), 5000);
    CHECK_EQ(fw_profile_freq(&p, 500000), 1000000);
    uint32_t prev = 0;
    for (uint32_t n = 0; n < p.accel_pulses; n += 997) { uint32_t f = fw_profile_freq(&p, n); CHECK(f >= prev); prev = f; }
    prev = 0xFFFFFFFFu;
    for (uint32_t n = p.decel_start; n < p.position; n += 997) { uint32_t f = fw_profile_freq(&p, n); CHECK(f <= prev); prev = f; }
    CHECK_RANGE(fw_profile_freq(&p, 999999), 5000, 6000);
    for (uint32_t n = 0; n < 1000000; n += 1009) { uint32_t f = fw_profile_freq(&p, n); CHECK(f >= 5000 && f <= 1000000); }
}

static void test_triangle_and_constant(void)
{
    fw_profile_t p;
    fw_profile_calc(&p, 1000, 100, 100000, 100, 100);                   /* too short to reach the speed */
    CHECK(p.peak < 100000.0f && p.peak > 100.0f);
    CHECK_EQ(p.accel_pulses + (p.position - p.decel_start), 1000);
    fw_profile_calc(&p, 1000, 5000, 5000, 100, 100);                    /* speed == start speed: constant */
    CHECK_EQ(p.accel_pulses, 0);
    CHECK_EQ(p.decel_start, 1000);
    CHECK_EQ(fw_profile_freq(&p, 500), 5000);
    fw_profile_calc(&p, 1, 100, 100, 10, 10);
    CHECK_EQ(fw_profile_freq(&p, 0), 100);
}

/* [T-profile-3] HMI stop: re-planned deceleration ends at n + d, frequency continuous, falls to the start speed */
static void test_plan_stop(void)
{
    fw_profile_t p;
    fw_profile_calc(&p, 1000000, 5000, 500000, 200, 400);
    uint32_t n0 = 300000, f0 = fw_profile_freq(&p, n0), stop_at = 0;
    CHECK_EQ(f0, 500000);
    CHECK(fw_profile_plan_stop(&p, n0, f0, &stop_at));
    CHECK(stop_at > n0 && stop_at < 1000000);
    const double d = ((double)f0 * f0 - 5000.0 * 5000.0) / p.k_dec;
    CHECK_RANGE(stop_at - n0, d - 2, d + 2);
    CHECK_RANGE(fw_profile_freq(&p, n0), f0 - 100, f0);                 /* continuous at the switch */
    CHECK_RANGE(fw_profile_freq(&p, stop_at), 5000, 5001);
    uint32_t prev = 0xFFFFFFFFu;
    for (uint32_t n = n0; n <= stop_at; n += 101) { uint32_t f = fw_profile_freq(&p, n); CHECK(f <= prev); prev = f; }
    /* a move that ends sooner than a decelerated stop is left alone */
    fw_profile_calc(&p, 100000, 5000, 500000, 200, 400);
    const fw_profile_t before = p;
    CHECK(!fw_profile_plan_stop(&p, 99000, 400000, &stop_at));
    CHECK_EQ(p.position, before.position);
    /* at/under the start speed the stop point is the current count */
    fw_profile_calc(&p, 1000, 5000, 500000, 200, 400);
    CHECK(fw_profile_plan_stop(&p, 10, 4000, &stop_at) && stop_at == 10);
}

static void test_crc(void)
{
    CHECK_EQ(fw_crc32("123456789", 9), 0xCBF43926u);
    CHECK_EQ(fw_crc16_modbus((const uint8_t *)"123456789", 9), 0x4B37);
    const uint8_t frame[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01};      /* well-known: CRC 0x0A84 -> bytes 84 0A */
    CHECK_EQ(fw_crc16_modbus(frame, sizeof(frame)), 0x0A84);
}

/* ---------------------------------------------------------------- STOP / START state machine (D-058) */
typedef struct { fw_es_t es; fw_start_t st; uint16_t edges, ignored; int fired; } sim_t;
static void sim_init(sim_t *s, bool start_active_at_boot, bool stop_active_at_boot)
{
    memset(s, 0, sizeof(*s));
    fw_es_init(&s->es);
    fw_start_init(&s->st, start_active_at_boot);
    fw_es_boot_check(&s->es, stop_active_at_boot);
}
/* run ms milliseconds with constant inputs; a STOP rising edge trips the latch as the ISR would */
static void sim_run(sim_t *s, unsigned ms, bool stop_in, bool start_raw, bool *prev_stop)
{
    for (unsigned i = 0; i < ms; ++i) {
        if (stop_in && !*prev_stop) fw_es_trip(&s->es, 0);
        *prev_stop = stop_in;
        if (fw_stopstart_step(&s->es, &s->st, stop_in, start_raw, 5, &s->edges, &s->ignored)) s->fired++;
    }
}

static void test_stop_start_basic(void)
{
    sim_t s; bool ps = false;
    sim_init(&s, false, false);
    CHECK(!fw_es_blocking(&s.es, false));
    sim_run(&s, 20, false, true, &ps);                                  /* START pressed, no STOP: starts once */
    CHECK_EQ(s.fired, 1);
    sim_run(&s, 100, false, true, &ps);                                 /* holding does not repeat */
    CHECK_EQ(s.fired, 1);
    sim_run(&s, 20, false, false, &ps);
    sim_run(&s, 20, false, true, &ps);
    CHECK_EQ(s.fired, 2);
}

static void test_start_debounce(void)
{
    sim_t s; bool ps = false;
    sim_init(&s, false, false);
    sim_run(&s, 3, false, true, &ps);                                   /* 3 ms glitch < 5 ms */
    sim_run(&s, 10, false, false, &ps);
    CHECK_EQ(s.fired, 0);
    sim_run(&s, 5, false, true, &ps);                                   /* exactly 5 ms */
    CHECK_EQ(s.fired, 1);
}

static void test_start_held_at_boot(void)
{
    sim_t s; bool ps = false;
    sim_init(&s, true, false);                                          /* START active at boot is not an edge */
    sim_run(&s, 50, false, true, &ps);
    CHECK_EQ(s.fired, 0);
    sim_run(&s, 10, false, false, &ps);
    sim_run(&s, 10, false, true, &ps);
    CHECK_EQ(s.fired, 1);
}

/* [T-stop-1] STOP trips the latch; START while STOP is active is ignored; held START does not restart after release;
 * a fresh START edge restarts only after the STOP input was inactive for 10 ms */
static void test_stop_latch_cycle(void)
{
    sim_t s; bool ps = false;
    sim_init(&s, false, false);
    sim_run(&s, 10, true, false, &ps);                                  /* STOP opens */
    CHECK(s.es.latched);
    CHECK_EQ(s.es.trip_count, 1);
    CHECK_EQ(fw_es_state(&s.es, true), 1);
    sim_run(&s, 20, true, true, &ps);                                   /* START pressed while STOP active */
    CHECK_EQ(s.fired, 0);
    CHECK(s.es.latched);
    sim_run(&s, 50, false, true, &ps);                                  /* STOP released, START still held: no edge */
    CHECK_EQ(s.fired, 0);
    CHECK(s.es.latched);                                                /* still latched, only a START edge frees it */
    CHECK_EQ(fw_es_state(&s.es, false), 2);
    CHECK(fw_es_blocking(&s.es, false));
    sim_run(&s, 10, false, false, &ps);
    sim_run(&s, 10, false, true, &ps);                                  /* new edge after release */
    CHECK_EQ(s.fired, 1);
    CHECK(!s.es.latched);
    CHECK(!fw_es_blocking(&s.es, false));
}

static void test_release_needs_stable_input(void)
{
    sim_t s; bool ps = false;
    sim_init(&s, false, false);
    sim_run(&s, 10, true, false, &ps);
    sim_run(&s, 3, false, false, &ps);                                  /* input inactive only 3 ms */
    sim_run(&s, 5, false, true, &ps);                                   /* START edge at 8 ms of stability: refused */
    CHECK_EQ(s.fired, 0);
    CHECK(s.es.latched);
    CHECK_EQ(s.ignored, 1);
    sim_run(&s, 10, false, false, &ps);
    sim_run(&s, 10, false, true, &ps);                                  /* later edge: ok */
    CHECK_EQ(s.fired, 1);
    CHECK(!s.es.latched);
}

static void test_stop_active_at_boot(void)                                /* review M-04 */
{
    sim_t s; bool ps = true;
    sim_init(&s, false, true);
    CHECK(s.es.latched);
    CHECK(fw_es_blocking(&s.es, true));
    sim_run(&s, 30, true, true, &ps);
    CHECK_EQ(s.fired, 0);
    sim_run(&s, 30, false, false, &ps);
    sim_run(&s, 10, false, true, &ps);
    CHECK_EQ(s.fired, 1);
    CHECK_EQ(fw_es_release(&s.es, true), FW_ES_REFUSED);                /* direct release refused while the input is active */
}

static void test_axes_independent(void)
{
    sim_t x, y; bool px = false, py = false;
    sim_init(&x, false, false); sim_init(&y, false, false);
    sim_run(&x, 10, true, false, &px);                                  /* X STOP */
    sim_run(&y, 10, false, true, &py);                                  /* Y START meanwhile */
    CHECK(x.es.latched && !y.es.latched);
    CHECK_EQ(y.fired, 1);
}

/* ---------------------------------------------------------------- parameter tables */
static void test_defaults_valid(void)
{
    fw_cfg_t c;
    fw_cfg_defaults(&c);
    CHECK_EQ(fw_cfg_verify(&c, sizeof(c)), FW_CFG_OK);
    for (unsigned a = 0; a < FW_MAX_AXES; ++a) {
        CHECK_EQ(fw_par_validate(c.par[a]), 0);
        CHECK_EQ(fw_op_validate(c.op[a], c.par[a], true), 0);
    }
    CHECK_EQ(fw_net_validate(c.net, true), 0);
    CHECK_EQ(fw_net_validate(c.net, false), 0);
    CHECK_EQ(fw_sys_validate(c.sys), 0);
    CHECK_EQ(c.net[FW_N_COMM_MODE], FW_MODE_TCP);                       /* factory default TCP */
    CHECK_EQ(fw_w32(c.net, FW_N_IP), 0xC0A800DCu);                      /* 192.168.0.220 */
    CHECK_EQ(c.net[FW_N_UNIT], 1);
    CHECK_EQ(fw_w32(c.par[0], FW_P_MAX_SPEED), 100000);
    CHECK_EQ(fw_w32(c.par[0], FW_P_PULSE_RES), 2000000);
    CHECK_EQ(fw_w32(c.leg, 0), 5000);
    CHECK_EQ(fw_w32(c.op[0], FW_O_SPEED), 10000);
    CHECK_EQ(c.op[0][FW_O_ACCEL], 400);
    CHECK_EQ(c.par[0][FW_P_SVON_MODE], 0);
    CHECK_EQ(c.par[0][FW_P_REQUIRE_HOME], 1);
    CHECK_EQ(c.sys[FW_S_AUTO_SAVE], 1);
    CHECK_EQ(c.sys[FW_S_MAX_CLIENTS], 4);
    CHECK_EQ((int32_t)fw_w32(c.par[0], FW_P_SWLIM_NEG), INT32_MIN);
}

/* [T-param-1] boundary values of the parameter ranges */
static void test_param_ranges(void)
{
    const fw_ent_t *e;
    e = fw_seg_find(&FW_SEG_PAR, FW_P_DIR_SETUP_US);
    CHECK(!fw_ent_check(e, 0) && fw_ent_check(e, 1) && fw_ent_check(e, 1000) && !fw_ent_check(e, 1001));
    e = fw_seg_find(&FW_SEG_PAR, FW_P_MAX_SPEED);
    CHECK(!fw_ent_check(e, 0) && fw_ent_check(e, 1) && fw_ent_check(e, 1000000) && !fw_ent_check(e, 1000001));
    e = fw_seg_find(&FW_SEG_PAR, FW_P_MAX_SPEED + 1);                   /* any word of a 32-bit item finds the item */
    CHECK(e && e->off == FW_P_MAX_SPEED);
    e = fw_seg_find(&FW_SEG_PAR, FW_P_PULSE_MODE);
    CHECK(fw_ent_check(e, 0) && !fw_ent_check(e, 1));                   /* CW/CCW not implemented */
    e = fw_seg_find(&FW_SEG_PAR, FW_P_SWLIM_EN);
    CHECK(fw_ent_check(e, 0) && !fw_ent_check(e, 1));
    e = fw_seg_find(&FW_SEG_PAR, FW_P_ACCEL_CURVE);
    CHECK(!fw_ent_check(e, 1));                                         /* S curve not implemented */
    e = fw_seg_find(&FW_SEG_PAR, FW_P_HOME_TIMEOUT);
    CHECK(fw_ent_check(e, 0) && fw_ent_check(e, 3600) && !fw_ent_check(e, 3601));
    e = fw_seg_find(&FW_SEG_PAR, FW_P_INPUT_FILTER);
    CHECK(fw_ent_check(e, 20) && !fw_ent_check(e, 21));
    e = fw_seg_find(&FW_SEG_OP, FW_O_ACCEL);
    CHECK(!fw_ent_check(e, 9) && fw_ent_check(e, 10) && fw_ent_check(e, 60000) && !fw_ent_check(e, 60001));
    e = fw_seg_find(&FW_SEG_OP, FW_O_SPEED);
    CHECK(!fw_ent_check(e, 99) && fw_ent_check(e, 100) && fw_ent_check(e, 1000000) && !fw_ent_check(e, 1000001));
    e = fw_seg_find(&FW_SEG_SYS, FW_S_COMMLOSS_MS);
    CHECK(!fw_ent_check(e, 99) && fw_ent_check(e, 100) && fw_ent_check(e, 60000) && !fw_ent_check(e, 60001));
    e = fw_seg_find(&FW_SEG_SYS, FW_S_MAX_CLIENTS);
    CHECK(!fw_ent_check(e, 0) && fw_ent_check(e, 1) && fw_ent_check(e, 8) && !fw_ent_check(e, 9));
    e = fw_seg_find(&FW_SEG_NET, FW_N_UNIT);
    CHECK(!fw_ent_check(e, 0) && fw_ent_check(e, 1) && fw_ent_check(e, 247) && !fw_ent_check(e, 248));
    CHECK(fw_seg_find(&FW_SEG_PAR, 0x2D) == NULL);
}

static void test_param_cross_rules(void)
{
    fw_cfg_t c;
    fw_cfg_defaults(&c);
    uint16_t *p = c.par[0];
    fw_set32(p, FW_P_START_SPEED, 200000);                              /* start_speed > max_speed (100,000) */
    CHECK_EQ(fw_par_validate(p), FW_P_START_SPEED + 1);
    fw_set32(p, FW_P_START_SPEED, 100000);
    CHECK_EQ(fw_par_validate(p), 0);
    fw_set32(p, FW_P_JOG_MAX, 100001);
    CHECK_EQ(fw_par_validate(p), FW_P_JOG_MAX + 1);
    fw_set32(p, FW_P_JOG_MAX, 50000); fw_set32(p, FW_P_JOG_SPEED, 50001);
    CHECK_EQ(fw_par_validate(p), FW_P_JOG_SPEED + 1);
    fw_set32(p, FW_P_JOG_SPEED, 10000); fw_set32(p, FW_P_HOME_CREEP, 20000);
    CHECK_EQ(fw_par_validate(p), FW_P_HOME_CREEP + 1);
    fw_set32(p, FW_P_HOME_CREEP, 500); fw_set32(p, FW_P_PULSE_RES, 3000000);
    CHECK_EQ(fw_par_validate(p), FW_P_PULSE_RES + 1);                   /* not an allowed resolution */
    fw_set32(p, FW_P_PULSE_RES, 10000000);
    CHECK_EQ(fw_par_validate(p), 0);
    /* move speed through the axis area is bound by max_speed and a manual start speed; the legacy window is not */
    fw_cfg_defaults(&c);
    fw_set32(c.op[0], FW_O_SPEED, 500000);
    CHECK_EQ(fw_op_validate(c.op[0], c.par[0], false), 0);
    CHECK_EQ(fw_op_validate(c.op[0], c.par[0], true), FW_O_SPEED + 1);
    fw_set32(c.par[0], FW_P_START_SPEED, 20000); fw_set32(c.op[0], FW_O_SPEED, 10000);
    CHECK_EQ(fw_op_validate(c.op[0], c.par[0], true), FW_O_SPEED + 1);   /* below the manual start speed */
}

static void test_net_validation(void)
{
    fw_cfg_t c;
    fw_cfg_defaults(&c);
    uint16_t *n = c.net;
    fw_set32(n, FW_N_IP, 0); CHECK_EQ(fw_net_validate(n, true), FW_N_IP + 1);
    fw_set32(n, FW_N_IP, 0xC0A800DCu);
    fw_set32(n, FW_N_MASK, 0xFF00FF00u); CHECK_EQ(fw_net_validate(n, true), FW_N_MASK + 1);
    fw_set32(n, FW_N_MASK, 0xFFFF0000u); CHECK_EQ(fw_net_validate(n, true), 0);
    fw_set32(n, FW_N_MASK, 0); CHECK(fw_net_validate(n, true) != 0);
    fw_set32(n, FW_N_MASK, 0xFFFFFF00u);
    n[FW_N_RTU_BAUD] = 144; CHECK_EQ(fw_net_validate(n, true), FW_N_RTU_BAUD + 1);
    n[FW_N_RTU_BAUD] = 1152;   /* 115200 baud */ CHECK_EQ(fw_net_validate(n, true), 0);
    n[FW_N_RTU_PARITY] = 3; CHECK(fw_net_validate(n, true) != 0);
    n[FW_N_RTU_PARITY] = 2; CHECK_EQ(fw_net_validate(n, true), 0);
    n[FW_N_RTU_ADDR] = 0; CHECK(fw_net_validate(n, true) != 0);
    n[FW_N_RTU_ADDR] = 5;
    n[FW_N_COMM_MODE] = FW_MODE_RTU;
    CHECK_EQ(fw_net_validate(n, true), 0);                              /* RTU model accepts RTU */
    CHECK_EQ(fw_net_validate(n, false), FW_N_COMM_MODE + 1);            /* TCP model refuses RTU (D-059) */
    n[FW_N_COMM_MODE] = 2; CHECK(fw_net_validate(n, true) != 0);
}

/* [T-mode-1] communication mode selection */
static void test_mode_selection(void)
{
    bool fb = true;
    CHECK(fw_mode_write_ok(FW_MODE_TCP, false));
    CHECK(fw_mode_write_ok(FW_MODE_TCP, true));
    CHECK(!fw_mode_write_ok(FW_MODE_RTU, false));
    CHECK(fw_mode_write_ok(FW_MODE_RTU, true));
    CHECK(!fw_mode_write_ok(2, true));
    CHECK_EQ(fw_mode_effective(FW_MODE_TCP, false, &fb), FW_MODE_TCP); CHECK(!fb);
    CHECK_EQ(fw_mode_effective(FW_MODE_RTU, true, &fb), FW_MODE_RTU); CHECK(!fb);
    CHECK_EQ(fw_mode_effective(FW_MODE_RTU, false, &fb), FW_MODE_TCP); CHECK(fb);   /* stored RTU, hardware missing */
    CHECK_EQ(fw_mode_effective(7, true, &fb), FW_MODE_TCP); CHECK(fb);
    CHECK_EQ(fw_mode_effective(FW_MODE_RTU, true, NULL), FW_MODE_RTU);
    fw_cfg_t c;
    fw_cfg_defaults(&c);
    CHECK_EQ(fw_mode_effective(c.net[FW_N_COMM_MODE], false, &fb), FW_MODE_TCP);   /* shipping default is TCP on every model */
}

static void test_cfg_integrity(void)
{
    fw_cfg_t c, d;
    fw_cfg_defaults(&c);
    CHECK_EQ(fw_cfg_verify(&c, sizeof(c) - 1), FW_CFG_BAD_SIZE);
    d = c; d.version = 9; fw_cfg_seal(&d); d.version = 9; CHECK_EQ(fw_cfg_verify(&d, sizeof(d)), FW_CFG_BAD_VERSION);
    d = c; d.par[0][FW_P_DIR_INVERT] = 1; CHECK_EQ(fw_cfg_verify(&d, sizeof(d)), FW_CFG_BAD_CRC);   /* bit flip without CRC */
    d = c; d.par[1][FW_P_DIR_SETUP_US] = 0; fw_cfg_seal(&d); CHECK_EQ(fw_cfg_verify(&d, sizeof(d)), FW_CFG_BAD_RANGE);
    d = c; d.leg[0] = 0; d.leg[1] = 0; fw_cfg_seal(&d); CHECK_EQ(fw_cfg_verify(&d, sizeof(d)), FW_CFG_BAD_RANGE);
    d = c; d.par[0][FW_P_DIR_INVERT] = 1; fw_cfg_seal(&d); CHECK_EQ(fw_cfg_verify(&d, sizeof(d)), FW_CFG_OK);
    d = c; d.net[FW_N_COMM_MODE] = FW_MODE_RTU; fw_cfg_seal(&d);
    CHECK_EQ(fw_cfg_verify(&d, sizeof(d)), FW_CFG_OK);                  /* mode vs hardware is judged at boot, not here */
}

/* [T-save-1] deferred, batched save; nothing while an axis runs */
static void test_save_schedule(void)
{
    fw_save_t s = {0};
    CHECK(!fw_save_due(&s, 100000, true, false));                       /* nothing dirty */
    fw_save_note_write(&s, 1000);
    CHECK(!fw_save_due(&s, 1000, true, false));
    CHECK(!fw_save_due(&s, 2999, true, false));
    CHECK(fw_save_due(&s, 3000, true, false));                          /* 2 s after the last write */
    fw_save_note_write(&s, 2900);                                       /* another write postpones (batching) */
    CHECK(!fw_save_due(&s, 3000, true, false));
    CHECK(fw_save_due(&s, 4900, true, false));
    CHECK(!fw_save_due(&s, 99999, true, true));                         /* axis running: never */
    CHECK(!fw_save_due(&s, 4900, false, false));                        /* auto_save off */
    fw_save_request(&s);
    CHECK(fw_save_due(&s, 4900, false, false));                         /* SAVE command */
    CHECK(!fw_save_due(&s, 4900, false, true));                         /* but not while running */
    fw_save_done(&s);
    CHECK(!fw_save_due(&s, 99999, true, false));
    /* wrap of the 32-bit ms counter */
    fw_save_note_write(&s, 0xFFFFFF00u);
    CHECK(!fw_save_due(&s, 0x100u, true, false));
    CHECK(fw_save_due(&s, 0x800u, true, false));
}

static void test_commloss(void)
{
    fw_commloss_t c;
    fw_commloss_init(&c, 0);
    CHECK(!fw_commloss_check(&c, false, 1000, 0, true, 50000));         /* disabled */
    CHECK(!fw_commloss_check(&c, true, 1000, 0, false, 50000));         /* nothing running */
    CHECK(!fw_commloss_check(&c, true, 1000, 0, true, 900));
    CHECK(fw_commloss_check(&c, true, 1000, 0, true, 1001));            /* scope 0: no request on any connection */
    fw_commloss_activity(&c, 7, false, 1500);
    CHECK(!fw_commloss_check(&c, true, 1000, 0, true, 2400));
    CHECK(fw_commloss_check(&c, true, 1000, 0, true, 2501));
    /* scope 1: the connection that sent the last command */
    fw_commloss_init(&c, 0);
    CHECK(!fw_commloss_check(&c, true, 1000, 1, true, 99999));          /* nobody commanded yet */
    fw_commloss_activity(&c, 3, true, 100);
    fw_commloss_activity(&c, 9, false, 5000);                           /* another connection stays busy */
    CHECK(fw_commloss_check(&c, true, 1000, 1, true, 1200));            /* commanding connection silent */
    fw_commloss_activity(&c, 3, false, 1150);
    CHECK(!fw_commloss_check(&c, true, 1000, 1, true, 1200));
    fw_commloss_conn_closed(&c, 3);
    CHECK(fw_commloss_check(&c, true, 1000, 1, true, 1200));            /* closed -> stop at once */
}

static void test_pin_conflict(void)
{
    const int ok[] = {25, 32, 20, 21, 33, 54, 19, 18, 17, 7, 8, 15, 16, 23, 26, 27, 46, 24, 47, 48, 51};
    CHECK(!fw_pin_conflict(ok, sizeof(ok) / sizeof(ok[0]), NULL, 0));
    const int dup[] = {25, 32, 25};
    CHECK(fw_pin_conflict(dup, 3, NULL, 0));
    const int a[] = {20, 21}, b[] = {1, 21};
    CHECK(fw_pin_conflict(a, 2, b, 2));
    const int neg[] = {-1, -1};
    CHECK(!fw_pin_conflict(neg, 2, NULL, 0));
}

static void test_reset_reason(void)
{
    CHECK(fw_rst_is_wdt(FW_RST_TASK_WDT) && fw_rst_is_wdt(FW_RST_INT_WDT) && fw_rst_is_wdt(FW_RST_OTHER_WDT));
    CHECK(!fw_rst_is_wdt(FW_RST_POWERON) && !fw_rst_is_wdt(FW_RST_PANIC));
}

int main(void)
{
    RUN(test_ticks); RUN(test_start_speed); RUN(test_trapezoid); RUN(test_triangle_and_constant); RUN(test_plan_stop);
    RUN(test_crc);
    RUN(test_stop_start_basic); RUN(test_start_debounce); RUN(test_start_held_at_boot); RUN(test_stop_latch_cycle);
    RUN(test_release_needs_stable_input); RUN(test_stop_active_at_boot); RUN(test_axes_independent);
    RUN(test_defaults_valid); RUN(test_param_ranges); RUN(test_param_cross_rules); RUN(test_net_validation);
    RUN(test_mode_selection); RUN(test_cfg_integrity); RUN(test_save_schedule); RUN(test_commloss);
    RUN(test_pin_conflict); RUN(test_reset_reason);
    SUMMARY();
}
