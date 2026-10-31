/* Host unit tests for tfw_logic (gcc, no ESP-IDF). Plain assert harness (Unity not installed). */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "tfw_logic.h"

static int g_run, g_fail;
#define CHECK(cond) do { g_run++; if (!(cond)) { g_fail++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define TEST(name) static void name(void)
#define RUN(name) do { int f0 = g_fail; name(); printf("%-52s %s\n", #name, g_fail == f0 ? "PASS" : "FAIL"); } while (0)

/* ===== TFW-02: frequency quantisation (risk 1) ===== */
TEST(t_ticks_never_exceed_request)
{
    const uint32_t res[] = {2000000u, 10000000u, 20000000u, 40000000u};
    for (unsigned r = 0; r < 4; ++r) {
        for (uint32_t f = 153; f <= 1000000u; f += (f < 5000 ? 7 : 997)) {
            uint32_t t;
            if (!tfw_ticks_for_freq(res[r], f, TFW_MAX_PERIOD_TICKS, &t)) continue;
            /* actual = res/t <= f  <=>  t*f >= res */
            CHECK((uint64_t)t * f >= res[r]);
            CHECK(t >= 2);
        }
    }
}
TEST(t_old_floor_rule_would_exceed)
{   /* documents the servo1axis behaviour: floor(2M/300k)=6 ticks -> 333333 Hz > 300000 */
    uint32_t old_ticks = 2000000u / 300000u;
    CHECK(2000000u / old_ticks > 300000u);
    uint32_t t; CHECK(tfw_ticks_for_freq(2000000u, 300000u, 65535, &t));
    CHECK(t == 7); CHECK(tfw_freq_actual(2000000u, t) == 285714u);
}
TEST(t_ticks_limits)
{
    uint32_t t = 0;
    CHECK(!tfw_ticks_for_freq(10000000u, 0, 65535, &t));
    CHECK(!tfw_ticks_for_freq(10000000u, 100, 65535, &t));          /* 100000 ticks > 65535: rejected, not clamped */
    CHECK(tfw_ticks_for_freq(10000000u, 153, 65535, &t) && t == 65360u);
    CHECK(tfw_ticks_for_freq(2000000u, 1000000u, 65535, &t) && t == 2 && tfw_cmp_for_ticks(t) == 1);
    CHECK(tfw_ticks_for_freq(2000000u, 5000000u, 65535, &t) && t == 2);   /* above res/2 -> res/2, lower than request */
    CHECK(tfw_freq_actual(2000000u, 2) == 1000000u);
    CHECK(tfw_res_valid(2000000u) && tfw_res_valid(10000000u) && tfw_res_valid(20000000u) && tfw_res_valid(40000000u));
    CHECK(!tfw_res_valid(0) && !tfw_res_valid(5000000u));
}
TEST(t_total_count)
{
    CHECK(tfw_total_count(0, 5) == 5);
    CHECK(tfw_total_count(30000, 7) == 30007);
    CHECK(tfw_total_count(0, -1) == 0);
    CHECK(tfw_total_count(0xFFFFFFF0u, 100) == 0xFFFFFFFFu);
    CHECK(tfw_target_remainder(30000) == 0 && tfw_target_remainder(30001) == 1 && tfw_target_remainder(29999) == 29999);
}

/* ===== TFW-02: profile ===== */
TEST(t_profile_trapezoid)
{
    tfw_profile_t p;
    tfw_profile_calc(&p, 1000000, 5000, 200000, 100, 100);
    CHECK(tfw_profile_freq(&p, 0) == 5000);
    CHECK(tfw_profile_freq(&p, 500000) == 200000);
    CHECK(p.accel_pulses > 0 && p.decel_start > p.accel_pulses && p.decel_start < 1000000);
    {   /* last pulse: 1 pulse of deceleration left -> sqrt(s^2 + 2*a_dec*1) */
        double ad = (200000.0 - 5000.0) / 0.1, ref1 = sqrt(5000.0 * 5000.0 + 2 * ad);
        CHECK(fabs((double)tfw_profile_freq(&p, 999999) - ref1) < 2.0);
    }
    uint32_t prev = 0;
    for (uint32_t n = 0; n < p.accel_pulses; n += 97) {
        uint32_t f = tfw_profile_freq(&p, n);
        CHECK(f >= prev); CHECK(f >= 5000 && f <= 200000); prev = f;
    }
    /* analytic: f(n) = sqrt(s^2 + 2 a n), a = (200000-5000)/0.1 s */
    double a = (200000.0 - 5000.0) / 0.1;
    uint32_t n = p.accel_pulses / 2;
    double ref = sqrt(5000.0 * 5000.0 + 2 * a * n);
    CHECK(fabs((double)tfw_profile_freq(&p, n) - ref) < ref * 1e-4 + 1);
}
TEST(t_profile_triangle_and_const)
{
    tfw_profile_t p;
    tfw_profile_calc(&p, 1000, 5000, 200000, 400, 400);   /* too short to reach 200 kHz */
    CHECK(p.peak < 200000.0f && p.peak > 5000.0f);
    uint32_t mx = 0;
    for (uint32_t n = 0; n < 1000; ++n) { uint32_t f = tfw_profile_freq(&p, n); if (f > mx) mx = f; CHECK(f >= 5000 && f <= 200000); }
    /* accel_pulses is truncated like the original, so the real maximum can exceed the analytic peak slightly (<0.5%) */
    CHECK(mx <= (uint32_t)(p.peak * 1.005f));
    tfw_profile_calc(&p, 5000, 3000, 3000, 100, 100);     /* start >= target: constant */
    CHECK(tfw_profile_freq(&p, 0) == 3000 && tfw_profile_freq(&p, 4999) == 3000);
    CHECK(tfw_start_speed(100) == 100 && tfw_start_speed(5000) == 5000 && tfw_start_speed(1000000) == 5000);
}
TEST(t_profile_never_above_target)
{
    uint32_t speeds[] = {100, 4999, 5000, 5001, 123457, 1000000};
    for (unsigned i = 0; i < 6; ++i) {
        tfw_profile_t p; uint32_t pos = 1 + (i * 7919u) % 300000u;
        tfw_profile_calc(&p, pos, tfw_start_speed(speeds[i]), speeds[i], 10 + i * 50, 60000);
        for (uint32_t n = 0; n < pos; n += (pos / 200 + 1)) CHECK(tfw_profile_freq(&p, n) <= speeds[i]);
    }
}

/* ===== TFW-02: update stats ===== */
TEST(t_upd_stats)
{
    tfw_upd_stats_t s = {0};
    tfw_upd_record(&s, 1000, 1000 + 4000, 400, 1000);          /* 10 us */
    CHECK(s.max_us == 10 && s.late == 0 && s.calls == 1);
    tfw_upd_record(&s, 1000 + 400000, 1000 + 400000 + 800, 400, 1000);   /* interval 1000 us, 2 us */
    CHECK(s.late == 0 && s.max_us == 10);
    tfw_upd_record(&s, 1000 + 400000 + 400u * 1600, 1000 + 400000 + 400u * 1600 + 40000, 400, 1000);  /* 1600 us interval */
    CHECK(s.late == 1 && s.max_us == 100);
    tfw_upd_record(&s, 0xFFFFFF00u, 0xFFFFFF00u + 4000, 400, 1000);    /* wrap-around safe */
    CHECK(s.max_us == 100);
}

/* ===== TFW-01: judge / random / load ===== */
TEST(t_judge)
{
    tfw_judge_t j = {0, 0};
    tfw_judge_move(&j, 1000, 1000); CHECK(j.err == 0 && j.overshoot == 0);
    tfw_judge_move(&j, 1000, 1003); CHECK(j.err == 1 && j.overshoot == 3);
    tfw_judge_move(&j, 1000, 998);  CHECK(j.err == 2 && j.overshoot == 3);
}
TEST(t_random)
{
    uint32_t s1 = 12345, s2 = 12345, seen[7] = {0};
    for (int i = 0; i < 200; ++i) { CHECK(tfw_rand_distance(&s1) == tfw_rand_distance(&s2)); }
    uint32_t s = 7;
    static const uint32_t set[] = {1u, 2u, 1000u, 29999u, 30000u, 30001u, 1000000u};
    for (int i = 0; i < 700; ++i) { uint32_t d = tfw_rand_distance(&s); for (int k = 0; k < 7; ++k) if (d == set[k]) seen[k]++; }
    for (int k = 0; k < 7; ++k) CHECK(seen[k] > 0);
    for (int i = 0; i < 500; ++i) {
        uint32_t sp, a, d; tfw_rand_motion(&s, 200000, &sp, &a, &d);
        CHECK(sp >= 20000 && sp <= 200000 && a >= 10 && a <= 200 && d >= 10 && d <= 200);
    }
    uint32_t sp, a, d; tfw_rand_motion(&s, 100, &sp, &a, &d); CHECK(sp == 100);
    CHECK(tfw_load_busy_ms(50, 100) == 50 && tfw_load_busy_ms(0, 100) == 0 && tfw_load_busy_ms(200, 100) == 90);
}

/* ===== TFW-01: register window ===== */
static void w32(tfw_regwin_t *w, tfw_wr_effect_t *fx, uint16_t a, uint32_t v, int expect_rc)
{
    uint16_t d[2] = {(uint16_t)v, (uint16_t)(v >> 16)};
    CHECK(tfw_regwin_write(w, a, 2, d, fx) == expect_rc);
}
static int w16(tfw_regwin_t *w, tfw_wr_effect_t *fx, uint16_t a, uint16_t v) { return tfw_regwin_write(w, a, 1, &v, fx); }
static uint32_t r32(tfw_regwin_t *w, tfw_results_t *r, uint16_t a)
{
    uint16_t d[2]; CHECK(tfw_regwin_read(w, r, a, 2, d) == 0); return d[0] | ((uint32_t)d[1] << 16);
}

TEST(t_regwin_all_inputs_roundtrip)
{
    tfw_regwin_t w; tfw_results_t r; tfw_wr_effect_t fx; memset(&r, 0, sizeof r); memset(&fx, 0, sizeof fx);
    tfw_regwin_init(&w, 0);
    CHECK(w16(&w, &fx, TFW_R_AXIS_MASK, 3) == 0);
    w32(&w, &fx, TFW_R_FREQ, 1000000u, 0);
    w32(&w, &fx, TFW_R_COUNT, 4294967295u, 0);
    CHECK(w16(&w, &fx, TFW_R_ACCEL_MS, 60000) == 0 && w16(&w, &fx, TFW_R_DECEL_MS, 10) == 0);
    w32(&w, &fx, TFW_R_REPEAT, 1000000u, 0); w32(&w, &fx, TFW_R_SEED, 0xDEADBEEFu, 0);
    CHECK(w16(&w, &fx, TFW_R_LOAD_NVS_MS, 100) == 0 && w16(&w, &fx, TFW_R_LOAD_CPU_PCT, 90) == 0);
    CHECK(w16(&w, &fx, TFW_R_CFG_UPD_US, 500) == 0);
    w32(&w, &fx, TFW_R_CFG_RES_HZ, 40000000u, 0);
    CHECK(w16(&w, &fx, TFW_R_CFG_FLAGS, 0x6) == 0);
    CHECK(w.status == TFW_ST_IDLE);
    CHECK(fx.cfg_changed && fx.load_changed);
    CHECK(r32(&w, &r, TFW_R_FREQ) == 1000000u && r32(&w, &r, TFW_R_COUNT) == 4294967295u);
    CHECK(r32(&w, &r, TFW_R_REPEAT) == 1000000u && r32(&w, &r, TFW_R_SEED) == 0xDEADBEEFu);
    CHECK(r32(&w, &r, TFW_R_CFG_RES_HZ) == 40000000u);
    uint16_t d[16];
    CHECK(tfw_regwin_read(&w, &r, TFW_R_AXIS_MASK, 1, d) == 0 && d[0] == 3);
    CHECK(tfw_regwin_read(&w, &r, TFW_R_ACCEL_MS, 2, d) == 0 && d[0] == 60000 && d[1] == 10);
    CHECK(tfw_regwin_read(&w, &r, TFW_R_LOAD_NVS_MS, 2, d) == 0 && d[0] == 100 && d[1] == 90);
    CHECK(tfw_regwin_read(&w, &r, TFW_R_CFG_FLAGS, 1, d) == 0 && d[0] == 0x6);
    CHECK(tfw_regwin_read(&w, &r, TFW_R_CMD, 1, d) == 0 && d[0] == 0);       /* read 0 */
}
TEST(t_regwin_out_of_range_sets_0x11)
{
    struct { uint16_t a; uint32_t bad; int words; } cases[] = {
        {TFW_R_AXIS_MASK, 0, 1}, {TFW_R_AXIS_MASK, 4, 1}, {TFW_R_FREQ, 99, 2}, {TFW_R_FREQ, 1000001u, 2},
        {TFW_R_COUNT, 0, 2}, {TFW_R_ACCEL_MS, 9, 1}, {TFW_R_DECEL_MS, 60001, 1}, {TFW_R_REPEAT, 0, 2},
        {TFW_R_REPEAT, 1000001u, 2}, {TFW_R_LOAD_NVS_MS, 5, 1}, {TFW_R_LOAD_NVS_MS, 10001, 1},
        {TFW_R_LOAD_CPU_PCT, 91, 1}, {TFW_R_CFG_UPD_US, 499, 1}, {TFW_R_CFG_UPD_US, 10001, 1},
        {TFW_R_CFG_RES_HZ, 3000000u, 2}, {TFW_R_CFG_FLAGS, 0x10, 1}, {TFW_R_CMD, 0, 1}, {TFW_R_CMD, 7, 1},
        {TFW_R_ES_PATH_SEL, 0, 1}, {TFW_R_ES_PATH_SEL, 4, 1}, {TFW_R_ES_RELEASE, 2, 1},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        tfw_regwin_t w; tfw_results_t r; tfw_wr_effect_t fx; memset(&r, 0, sizeof r); memset(&fx, 0, sizeof fx);
        tfw_regwin_init(&w, 0);
        tfw_regwin_t before = w;
        uint16_t d[2] = {(uint16_t)cases[i].bad, (uint16_t)(cases[i].bad >> 16)};
        CHECK(tfw_regwin_write(&w, cases[i].a, cases[i].words, d, &fx) == 0);
        CHECK(w.status == TFW_ST_BAD_VALUE);
        CHECK(fx.cmd == 0 && !fx.es_release && !fx.es_path_set);
        w.status = 0;
        CHECK(memcmp(&w, &before, sizeof w) == 0);            /* value not stored */
    }
}
TEST(t_regwin_exceptions)
{
    tfw_regwin_t w; tfw_wr_effect_t fx; tfw_regwin_init(&w, 0); memset(&fx, 0, sizeof fx);
    uint16_t d[4] = {1, 0, 0, 0};
    CHECK(tfw_regwin_write(&w, TFW_R_SEQ_DONE, 1, d, &fx) == 2);        /* read-only */
    CHECK(tfw_regwin_write(&w, TFW_R_STATUS, 1, d, &fx) == 2);
    CHECK(tfw_regwin_write(&w, 0x7100, 1, d, &fx) == 2);                /* outside window */
    CHECK(tfw_regwin_write(&w, 0x70FF, 2, d, &fx) == 2);
    CHECK(tfw_regwin_write(&w, 0x7014, 1, d, &fx) == 2);                /* unmapped */
    CHECK(tfw_regwin_write(&w, TFW_R_FREQ, 1, d, &fx) == 3);            /* half of a 32-bit item */
    CHECK(tfw_regwin_write(&w, TFW_R_FREQ + 1, 1, d, &fx) == 3);
    d[0] = 5; CHECK(tfw_regwin_write(&w, TFW_R_AXIS_MASK, 2, d, &fx) == 3);   /* mask + half of freq: atomic reject */
    CHECK(w.axis_mask == 1);
    tfw_results_t r; memset(&r, 0, sizeof r);
    uint16_t out[8];
    CHECK(tfw_regwin_read(&w, &r, 0x7100, 1, out) == 2);
    CHECK(tfw_regwin_read(&w, &r, 0x70FF, 2, out) == 2);
    CHECK(tfw_regwin_read(&w, &r, 0x7014, 1, out) == 0 && out[0] == 0);       /* reserved reads 0 */
}
TEST(t_regwin_outputs_map)
{
    tfw_regwin_t w; tfw_results_t r; memset(&r, 0, sizeof r); tfw_regwin_init(&w, TFW_CFG_PCNT_IRAM);
    r.seq_done = 0x12345678u; r.err[0] = 1; r.err[1] = 2; r.overshoot[0] = 3; r.overshoot[1] = 4;
    r.upd_late = 5; r.upd_max_us = 6; r.isr_lat_ns[0] = 7; r.isr_lat_ns[1] = 8; r.nvs_writes = 9; r.nvs_max_ms = 10;
    r.chip_rev = 301; r.cpu_mhz = 400; r.last_target[0] = 11; r.last_pcnt[0] = 12; r.last_target[1] = 13; r.last_pcnt[1] = 14;
    r.mb_clients = 4; r.mb_link_down = 2; r.mb_req_count = 0x10001u; r.mb_proc_max_us = 99;
    for (int i = 0; i < 8; ++i) r.mb_hist[i] = (uint16_t)(100 + i);
    r.es_state = 2; r.es_trip_count = 3; r.es_isr_lat_ns = 4000; r.es_pulses_after[0] = 21; r.es_pulses_after[1] = 22;
    r.es_bind_ok = 3; r.es_boot_level = 1; r.es_last_err = 0x101;
    w.status = TFW_ST_DONE;
    CHECK(r32(&w, &r, TFW_R_SEQ_DONE) == 0x12345678u);
    CHECK(r32(&w, &r, TFW_R_ERR_AX1) == 1 && r32(&w, &r, TFW_R_ERR_AX2) == 2);
    CHECK(r32(&w, &r, TFW_R_OVS_AX1) == 3 && r32(&w, &r, TFW_R_OVS_AX2) == 4);
    CHECK(r32(&w, &r, TFW_R_UPD_LATE) == 5 && r32(&w, &r, TFW_R_UPD_MAX_US) == 6);
    CHECK(r32(&w, &r, TFW_R_ISR_LAT_AX1) == 7 && r32(&w, &r, TFW_R_ISR_LAT_AX2) == 8);
    CHECK(r32(&w, &r, TFW_R_NVS_WRITES) == 9 && r32(&w, &r, TFW_R_NVS_MAX_MS) == 10);
    CHECK(r32(&w, &r, TFW_R_LAST_TGT_AX1) == 11 && r32(&w, &r, TFW_R_LAST_CNT_AX1) == 12);
    CHECK(r32(&w, &r, TFW_R_LAST_TGT_AX2) == 13 && r32(&w, &r, TFW_R_LAST_CNT_AX2) == 14);
    CHECK(r32(&w, &r, TFW_R_MB_REQ_COUNT) == 0x10001u && r32(&w, &r, TFW_R_MB_PROC_MAX_US) == 99);
    CHECK(r32(&w, &r, TFW_R_ES_ISR_LAT) == 4000 && r32(&w, &r, TFW_R_ES_PULSES_AX1) == 21 && r32(&w, &r, TFW_R_ES_PULSES_AX2) == 22);
    uint16_t d[0x30];
    CHECK(tfw_regwin_read(&w, &r, 0x7036, 3, d) == 0 && d[0] == 301 && d[1] == 400 && d[2] == TFW_ST_DONE);
    CHECK(tfw_regwin_read(&w, &r, TFW_R_MB_CLIENTS, 2, d) == 0 && d[0] == 4 && d[1] == 2);
    CHECK(tfw_regwin_read(&w, &r, TFW_R_MB_HIST, 8, d) == 0 && d[0] == 100 && d[7] == 107);
    CHECK(tfw_regwin_read(&w, &r, TFW_R_ES_STATE, 2, d) == 0 && d[0] == 2 && d[1] == 3);
    CHECK(tfw_regwin_read(&w, &r, TFW_R_ES_BIND_OK, 3, d) == 0 && d[0] == 3 && d[1] == 1 && d[2] == 0x101);
    CHECK(tfw_regwin_read(&w, &r, TFW_R_CFG_FLAGS, 1, d) == 0 && d[0] == TFW_CFG_PCNT_IRAM);
}
TEST(t_regwin_cfg_flags_b0_is_build_only)
{
    tfw_regwin_t w; tfw_wr_effect_t fx; memset(&fx, 0, sizeof fx); tfw_regwin_init(&w, TFW_CFG_PCNT_IRAM);
    CHECK(w16(&w, &fx, TFW_R_CFG_FLAGS, 0x0) == 0);
    CHECK(w.cfg_flags == TFW_CFG_PCNT_IRAM);
    CHECK(w16(&w, &fx, TFW_R_CFG_FLAGS, 0xE) == 0);
    CHECK(w.cfg_flags == (TFW_CFG_PCNT_IRAM | 0xE));
}
TEST(t_regwin_effects)
{
    tfw_regwin_t w; tfw_wr_effect_t fx; memset(&fx, 0, sizeof fx); tfw_regwin_init(&w, 0);
    CHECK(w16(&w, &fx, TFW_R_CMD, TFW_CMD_RUN_SEQ) == 0 && fx.cmd == TFW_CMD_RUN_SEQ);
    CHECK(w16(&w, &fx, TFW_R_ES_PATH_SEL, 2) == 0 && fx.es_path_set && w.es_path_sel == 2);
    CHECK(w16(&w, &fx, TFW_R_ES_RELEASE, 1) == 0 && fx.es_release);
}
TEST(t_regwin_running_ignores_cfg_and_keeps_status)
{
    tfw_regwin_t w; tfw_wr_effect_t fx; memset(&fx, 0, sizeof fx); tfw_regwin_init(&w, 0);
    w.status = TFW_ST_RUNNING;
    CHECK(w16(&w, &fx, TFW_R_CFG_UPD_US, 2000) == 0);
    CHECK(w.cfg_upd_us == 1000 && !fx.cfg_changed && w.status == TFW_ST_RUNNING);
}
TEST(t_cmd_check)
{
    tfw_regwin_t w; tfw_regwin_init(&w, 0);
    CHECK(tfw_cmd_check(&w, TFW_CMD_RUN_SEQ, false, false, true) == 0);
    CHECK(tfw_cmd_check(&w, TFW_CMD_RUN_SEQ, true, false, true) == TFW_ST_REJECT_BUSY);       /* running */
    CHECK(tfw_cmd_check(&w, TFW_CMD_RUN_MOVE, false, true, true) == TFW_ST_REJECT_BUSY);      /* e-stop latched */
    CHECK(tfw_cmd_check(&w, TFW_CMD_RUN_FIXED, false, true, true) == TFW_ST_REJECT_BUSY);
    w.axis_mask = 2;
    CHECK(tfw_cmd_check(&w, TFW_CMD_RUN_MOVE, false, false, false) == TFW_ST_NO_RESOURCE);    /* axis 2 init failed */
    CHECK(tfw_cmd_check(&w, TFW_CMD_RUN_MOVE, false, false, true) == 0);
    w.axis_mask = 1;
    CHECK(tfw_cmd_check(&w, TFW_CMD_RUN_MOVE, false, false, false) == 0);                    /* axis 1 does not need axis 2 */
    CHECK(tfw_cmd_check(&w, TFW_CMD_STOP_ALL, true, true, false) == 0);
    CHECK(tfw_cmd_check(&w, TFW_CMD_CLEAR_STATS, true, false, true) == TFW_ST_REJECT_BUSY);
    CHECK(tfw_cmd_check(&w, TFW_CMD_CLEAR_STATS, false, false, true) == 0);
    CHECK(tfw_cmd_check(&w, TFW_CMD_SET_LOAD, true, false, true) == 0);
    CHECK(tfw_cmd_check(&w, 99, false, false, true) == TFW_ST_BAD_VALUE);
}

/* ===== TFW-01: sequencer with a fake two-axis machine ===== */
typedef struct {
    uint32_t target[2], steps_left[2]; bool busy[2], started[2];
    uint32_t pcnt[2], now;
    int inject_off_by[2];         /* added to pcnt at completion */
    int inject_every;             /* inject on every n-th move of axis 0 (0 = never) */
    uint32_t moves[2];
    bool abort_flag; uint32_t abort_after_ms;
    bool fail_start[2];
    uint32_t stops;
    bool trip_when_idle_after_move;   /* R2 N-03: aborted() is true once axis 0 has moved and nothing is busy (trip between iterations) */
    bool trip_after_ax0_start;        /* R2 N-03: aborted() is true as soon as axis 0 was started */
    bool trip_on_first_move_call;     /* R2 N-03: the first move call fails and the trip flag appears (race with the ISR) */
    bool tripped;
} fake_t;
static int f_move(void *c, uint8_t ax, uint32_t sp, uint32_t cnt, uint32_t a, uint32_t d)
{
    fake_t *f = c; (void)sp; (void)a; (void)d;
    if (f->fail_start[ax]) return -1;
    if (f->trip_on_first_move_call && !f->tripped) { f->tripped = true; return -1; }
    f->target[ax] = cnt; f->pcnt[ax] = 0; f->busy[ax] = true; f->steps_left[ax] = 3; f->moves[ax]++;
    return 0;
}
static int f_fixed(void *c, uint8_t ax, uint32_t fr, uint32_t cnt) { return f_move(c, ax, fr, cnt, 0, 0); }
static bool f_busy(void *c, uint8_t ax) { fake_t *f = c; return f->busy[ax]; }
static uint32_t f_count(void *c, uint8_t ax) { fake_t *f = c; return f->pcnt[ax]; }
static void f_stop(void *c, uint8_t ax) { fake_t *f = c; f->busy[ax] = false; f->stops++; }
static void f_delay(void *c, uint32_t ms)
{
    fake_t *f = c; f->now += ms;
    for (int ax = 0; ax < 2; ++ax) if (f->busy[ax]) {
        if (f->steps_left[ax] > 0) f->steps_left[ax]--;
        if (f->steps_left[ax] == 0) {
            f->pcnt[ax] = f->target[ax];
            if (ax == 0 && f->inject_every && (f->moves[0] % (uint32_t)f->inject_every) == 0) f->pcnt[0] += f->inject_off_by[0];
            if (ax == 1 && f->inject_every && (f->moves[1] % (uint32_t)f->inject_every) == 0) f->pcnt[1] += f->inject_off_by[1];
            f->busy[ax] = false;
        }
    }
}
static uint32_t f_now(void *c) { return ((fake_t *)c)->now; }
static bool f_aborted(void *c)
{
    fake_t *f = c;
    if (f->tripped) return true;
    if (f->trip_when_idle_after_move && f->moves[0] >= 1 && !f->busy[0] && !f->busy[1]) return true;
    if (f->trip_after_ax0_start && f->moves[0] >= 1) return true;
    return f->abort_flag && f->now >= f->abort_after_ms;
}
static tfw_seq_hw_t hw_for(fake_t *f)
{
    tfw_seq_hw_t h = {f, f_move, f_fixed, f_busy, f_count, f_stop, f_delay, f_now, f_aborted};
    return h;
}
TEST(t_seq_1000_iterations_two_axes)
{
    fake_t f; memset(&f, 0, sizeof f); tfw_seq_hw_t h = hw_for(&f);
    tfw_regwin_t w; tfw_regwin_init(&w, 0); static volatile tfw_results_t res; memset((void *)&res, 0, sizeof res);
    w.axis_mask = 3; w.repeat = 1000; w.count = 1000; w.seed = 0;
    uint32_t n = tfw_seq_run(&h, &w, &res, TFW_CMD_RUN_SEQ);
    CHECK(n == 1000); CHECK(res.seq_done == 1000);
    CHECK(res.err[0] == 0 && res.err[1] == 0 && res.overshoot[0] == 0 && res.overshoot[1] == 0);
    CHECK(f.moves[0] == 1000 && f.moves[1] == 1000);
    CHECK(w.status == TFW_ST_DONE);
    CHECK(res.last_target[0] == 1000 && res.last_pcnt[1] == 1000);
}
TEST(t_seq_counters_consistent_with_injected_faults)
{
    fake_t f; memset(&f, 0, sizeof f); tfw_seq_hw_t h = hw_for(&f);
    tfw_regwin_t w; tfw_regwin_init(&w, 0); static volatile tfw_results_t res; memset((void *)&res, 0, sizeof res);
    w.axis_mask = 3; w.repeat = 100; w.count = 5000;
    f.inject_every = 10; f.inject_off_by[0] = 2; f.inject_off_by[1] = -1;   /* ax1 +2 (overshoot), ax2 -1 (short) */
    tfw_seq_run(&h, &w, &res, TFW_CMD_RUN_SEQ);
    CHECK(res.seq_done == 100);
    CHECK(res.err[0] == 10 && res.overshoot[0] == 20);
    CHECK(res.err[1] == 10 && res.overshoot[1] == 0);
    CHECK(res.last_target[0] == 5000);
}
TEST(t_seq_random_reproducible_by_seed)
{
    uint32_t t1[2][40], t2[2][40];
    for (int run = 0; run < 2; ++run) {
        fake_t f; memset(&f, 0, sizeof f);
        tfw_regwin_t w; tfw_regwin_init(&w, 0); static volatile tfw_results_t res; memset((void *)&res, 0, sizeof res);
        w.axis_mask = 3; w.repeat = 1; w.seed = 424242; w.freq = 100000;
        tfw_seq_hw_t h = hw_for(&f);
        for (int i = 0; i < 40; ++i) {
            w.seed = 424242;     /* same seed each call just to compare first targets */
            tfw_seq_run(&h, &w, &res, TFW_CMD_RUN_SEQ);
            (run ? t2 : t1)[0][i] = res.last_target[0]; (run ? t2 : t1)[1][i] = res.last_target[1];
        }
    }
    CHECK(memcmp(t1, t2, sizeof t1) == 0);
    CHECK(t1[0][0] != t1[1][0] || t1[0][0] != 0);
}
TEST(t_seq_move_and_fixed_single_axis)
{
    fake_t f; memset(&f, 0, sizeof f); tfw_seq_hw_t h = hw_for(&f);
    tfw_regwin_t w; tfw_regwin_init(&w, 0); static volatile tfw_results_t res; memset((void *)&res, 0, sizeof res);
    w.axis_mask = 2; w.count = 77; w.repeat = 50;
    CHECK(tfw_seq_run(&h, &w, &res, TFW_CMD_RUN_MOVE) == 1);
    CHECK(f.moves[0] == 0 && f.moves[1] == 1 && res.seq_done == 1 && res.last_pcnt[1] == 77);
    CHECK(tfw_seq_run(&h, &w, &res, TFW_CMD_RUN_FIXED) == 1);
    CHECK(f.moves[1] == 2 && res.seq_done == 2);
}
TEST(t_seq_abort_and_start_failure)
{
    fake_t f; memset(&f, 0, sizeof f); tfw_seq_hw_t h = hw_for(&f);
    tfw_regwin_t w; tfw_regwin_init(&w, 0); static volatile tfw_results_t res; memset((void *)&res, 0, sizeof res);
    w.axis_mask = 3; w.repeat = 1000; w.count = 1000;
    f.abort_flag = true; f.abort_after_ms = 10;
    uint32_t n = tfw_seq_run(&h, &w, &res, TFW_CMD_RUN_SEQ);
    CHECK(n < 1000 && n > 0); CHECK(w.status == TFW_ST_IDLE); CHECK(f.stops >= 1);
    memset(&f, 0, sizeof f); memset((void *)&res, 0, sizeof res); h = hw_for(&f);
    f.fail_start[1] = true; w.repeat = 5;
    n = tfw_seq_run(&h, &w, &res, TFW_CMD_RUN_SEQ);
    CHECK(n == 5 && res.err[1] == 5 && res.err[0] == 0);
}

/* ===== TFW-04: e-stop latch ===== */
TEST(t_estop_latch_rules)
{
    tfw_es_t e; tfw_es_init(&e);
    CHECK(tfw_es_state(&e, false) == 0 && !e.latched);
    tfw_es_trip(&e, 1000, 2000);
    CHECK(e.latched && e.trip_count == 1 && e.snap[0] == 1000 && e.snap[1] == 2000);
    CHECK(tfw_es_state(&e, true) == 1);
    CHECK(tfw_es_release(&e, true) == TFW_ST_REJECT_BUSY && e.latched);          /* input active -> refused */
    tfw_es_input_sample(&e, false, 5);
    CHECK(tfw_es_state(&e, false) == 2);
    CHECK(tfw_es_release(&e, false) == TFW_ST_REJECT_BUSY && e.latched);         /* not yet stable 10 ms */
    tfw_es_input_sample(&e, true, 1);                                            /* bounce resets the timer */
    tfw_es_input_sample(&e, false, 9);
    CHECK(tfw_es_release(&e, false) == TFW_ST_REJECT_BUSY);
    tfw_es_input_sample(&e, false, 2);
    CHECK(tfw_es_release(&e, false) == 0 && !e.latched);
    CHECK(tfw_es_state(&e, false) == 0);
    CHECK(e.trip_count == 1);                                                    /* no automatic re-trip */
    CHECK(tfw_es_pulses_after(1000, 1234) == 234 && tfw_es_pulses_after(1000, 900) == 0);
    CHECK(tfw_es_path_valid(1) && tfw_es_path_valid(2) && tfw_es_path_valid(3) && !tfw_es_path_valid(0) && !tfw_es_path_valid(4));
}
TEST(t_estop_blocks_run_commands)
{
    tfw_regwin_t w; tfw_es_t e; tfw_regwin_init(&w, 0); tfw_es_init(&e); tfw_es_trip(&e, 0, 0);
    CHECK(tfw_cmd_check(&w, TFW_CMD_RUN_MOVE, false, e.latched, true) == TFW_ST_REJECT_BUSY);
    tfw_es_input_sample(&e, false, 20); CHECK(tfw_es_release(&e, false) == 0);
    CHECK(tfw_cmd_check(&w, TFW_CMD_RUN_MOVE, false, e.latched, true) == 0);
}

/* ===== TFW-03: Modbus PDU + dispatcher ===== */
static tfw_legacy_t g_leg; static tfw_regwin_t g_win; static tfw_results_t g_res;
static void get_res(void *u, tfw_results_t *o) { (void)u; *o = g_res; }
static tfw_mb_ctx_t mk(void)
{
    tfw_legacy_init(&g_leg); tfw_regwin_init(&g_win, 0); memset(&g_res, 0, sizeof g_res);
    tfw_mb_ctx_t c; memset(&c, 0, sizeof c); c.legacy = &g_leg; c.win = &g_win; c.get_results = get_res; return c;
}
static size_t pdu(tfw_mb_ctx_t *c, const uint8_t *req, size_t n, uint8_t *resp) { return tfw_mb_process(c, req, n, resp); }

TEST(t_mb_legacy_read_write)
{
    tfw_mb_ctx_t c = mk(); uint8_t r[260];
    uint8_t rd[] = {3, 0, 0, 0, 4};
    CHECK(pdu(&c, rd, 5, r) == 2 + 8 && r[0] == 3 && r[1] == 8);
    CHECK(r[2] == 0x13 && r[3] == 0x88);                      /* position 5000 low word first: 0x1388 */
    CHECK(r[6] == 0x27 && r[7] == 0x10);                      /* speed 10000 */
    uint8_t wr[] = {16, 0, 0, 0, 4, 8, 0x03, 0xE8, 0, 0, 0x4E, 0x20, 0, 0};   /* pos=1000, speed=20000 */
    CHECK(pdu(&c, wr, sizeof wr, r) == 5 && r[0] == 16 && r[4] == 4);
    CHECK(tfw_legacy_u32(&g_leg, TFW_HR_POS) == 1000 && tfw_legacy_u32(&g_leg, TFW_HR_SPEED) == 20000);
    CHECK(tfw_legacy_u32(&g_leg, TFW_HR_START_SPD) == 5000);
    CHECK(g_leg.dirty);
    uint8_t w6[] = {6, 0, 2, 0x00, 0x32};                      /* speed low = 50 -> speed 50 < 100 invalid */
    CHECK(pdu(&c, w6, 5, r) == 2 && r[0] == 0x86 && r[1] == 3);
    CHECK(tfw_legacy_u32(&g_leg, TFW_HR_SPEED) == 20000);
}
TEST(t_mb_legacy_exceptions)
{
    tfw_mb_ctx_t c = mk(); uint8_t r[260];
    uint8_t a[] = {3, 0, 30, 0, 3};                            /* 30..32 beyond 32 regs */
    CHECK(pdu(&c, a, 5, r) == 2 && r[1] == 2);
    uint8_t b[] = {6, 0, 4, 0, 1};                             /* HR 4: not writable */
    CHECK(pdu(&c, b, 5, r) == 2 && r[0] == 0x86 && r[1] == 2);
    uint8_t d[] = {6, 0, 10, 0, 1};                            /* status read-only */
    CHECK(pdu(&c, d, 5, r) == 2 && r[1] == 2);
    uint8_t e[] = {43, 0, 0};
    CHECK(pdu(&c, e, 3, r) == 2 && r[0] == (43 | 0x80) && r[1] == 1);
    uint8_t f[] = {3, 0, 0, 0};
    CHECK(pdu(&c, f, 4, r) == 2 && r[1] == 3);
    uint8_t g[] = {3, 0, 0, 0, 0};
    CHECK(pdu(&c, g, 5, r) == 2 && r[1] == 2);
    CHECK(pdu(&c, f, 0, r) == 0);
    uint8_t h[] = {16, 0, 0, 0, 2, 3, 0, 0, 0};                // byte count mismatch
    CHECK(pdu(&c, h, 9, r) == 2 && r[1] == 3);
}
TEST(t_mb_coils_and_di)
{
    tfw_mb_ctx_t c = mk(); uint8_t r[260];
    uint8_t on[] = {5, 0, 0, 0xFF, 0x00};
    CHECK(pdu(&c, on, 5, r) == 5 && (g_leg.coils & 1));
    uint8_t rc[] = {1, 0, 0, 0, 8};
    CHECK(pdu(&c, rc, 5, r) == 3 && r[1] == 1 && r[2] == 0x09);   /* START + DIR */
    uint8_t bad[] = {5, 0, 0, 0x12, 0x34};
    CHECK(pdu(&c, bad, 5, r) == 2 && r[1] == 3);
    uint8_t out_of_range[] = {5, 0, 8, 0, 0};
    CHECK(pdu(&c, out_of_range, 5, r) == 2 && r[1] == 2);
    c.link = true; c.clients = 2; c.moving = true;
    uint8_t di[] = {2, 0, 0, 0, 5};
    CHECK(pdu(&c, di, 5, r) == 3 && r[2] == 0x07);
    uint8_t f15[] = {15, 0, 4, 0, 4, 1, 0x0F};
    CHECK(pdu(&c, f15, 7, r) == 5 && (g_leg.coils & 0xF0) == 0xF0);
}
TEST(t_mb_window_125_registers_and_writes)
{
    tfw_mb_ctx_t c = mk(); uint8_t r[260];
    g_res.seq_done = 7;
    uint8_t rd[] = {3, 0x70, 0x00, 0, 125};
    CHECK(pdu(&c, rd, 5, r) == 2 + 250 && r[1] == 250);
    CHECK(r[2 + 2 * 0x20 + 1] == 7);                           /* 0x7020 low word */
    uint8_t wr[] = {16, 0x70, 0x02, 0, 2, 4, 0x42, 0x40, 0x00, 0x0F};   /* freq = 0x000F4240 = 1,000,000 */
    CHECK(pdu(&c, wr, sizeof wr, r) == 5 && g_win.freq == 1000000u);
    uint8_t ro[] = {6, 0x70, 0x20, 0, 1};
    CHECK(pdu(&c, ro, 5, r) == 2 && r[1] == 2);
    uint8_t half[] = {6, 0x70, 0x02, 0, 1};
    CHECK(pdu(&c, half, 5, r) == 2 && r[1] == 3);
    uint8_t oor[] = {6, 0x70, 0x01, 0, 9};                      /* axis mask 9 -> status 0x11 */
    CHECK(pdu(&c, oor, 5, r) == 5 && g_win.status == TFW_ST_BAD_VALUE);
    uint8_t cmd[] = {6, 0x70, 0x00, 0, TFW_CMD_RUN_SEQ};
    CHECK(pdu(&c, cmd, 5, r) == 5 && c.fx.cmd == TFW_CMD_RUN_SEQ);
    uint8_t beyond[] = {3, 0x70, 0xF0, 0, 20};
    CHECK(pdu(&c, beyond, 5, r) == 2 && r[1] == 2);
    uint8_t above[] = {3, 0x80, 0x00, 0, 1};
    CHECK(pdu(&c, above, 5, r) == 2 && r[1] == 2);
}
TEST(t_mb_no_persist_side_effect)
{   /* risk 3: the PDU engine itself never touches storage; it only flags 'dirty' for legacy values */
    tfw_mb_ctx_t c = mk(); uint8_t r[260];
    uint8_t w[] = {6, 0, 0, 0x01, 0x00};
    g_leg.dirty = false;
    CHECK(pdu(&c, w, 5, r) == 5 && g_leg.dirty);
    g_leg.dirty = false;
    uint8_t win[] = {6, 0x70, 0x0D, 0, 10};
    CHECK(pdu(&c, win, 5, r) == 5 && !g_leg.dirty);            /* test-window writes never mark legacy dirty */
}
TEST(t_hist_bins)
{
    CHECK(tfw_hist_bin(0) == 0 && tfw_hist_bin(49) == 0 && tfw_hist_bin(50) == 1 && tfw_hist_bin(99) == 1);
    CHECK(tfw_hist_bin(100) == 2 && tfw_hist_bin(499) == 3 && tfw_hist_bin(999) == 4 && tfw_hist_bin(1999) == 5);
    CHECK(tfw_hist_bin(4999) == 6 && tfw_hist_bin(5000) == 7 && tfw_hist_bin(0xFFFFFFFFu) == 7);
}

/* ===== review round 1 regressions ===== */
TEST(t_r1_estop_active_at_boot_latches_and_blocks)      /* M-04 */
{
    tfw_es_t e; tfw_regwin_t w; tfw_regwin_init(&w, 0); tfw_es_init(&e);
    CHECK(!tfw_es_blocking(&e, false));
    CHECK(tfw_es_blocking(&e, true));                     /* input active, never an edge: still blocking */
    tfw_es_boot_check(&e, true);
    CHECK(e.latched && e.trip_count == 1);
    CHECK(tfw_cmd_check(&w, TFW_CMD_RUN_SEQ, false, tfw_es_blocking(&e, true), true) == TFW_ST_REJECT_BUSY);
    CHECK(tfw_cmd_check(&w, TFW_CMD_RUN_FIXED, false, tfw_es_blocking(&e, true), true) == TFW_ST_REJECT_BUSY);
    CHECK(tfw_es_release(&e, true) == TFW_ST_REJECT_BUSY && e.latched);        /* still active: no release */
    tfw_es_input_sample(&e, false, 20);
    CHECK(tfw_es_release(&e, false) == 0 && !e.latched);
    CHECK(tfw_cmd_check(&w, TFW_CMD_RUN_SEQ, false, tfw_es_blocking(&e, false), true) == 0);
    tfw_es_t f; tfw_es_init(&f); tfw_es_boot_check(&f, false);                  /* inactive at boot: nothing */
    CHECK(!f.latched && f.trip_count == 0);
    tfw_es_boot_check(&e, true); tfw_es_boot_check(&e, true);                   /* idempotent */
    CHECK(e.trip_count == 2);
}
TEST(t_r1_input_active_blocks_even_when_not_latched)    /* M-04 */
{
    tfw_es_t e; tfw_es_init(&e);
    CHECK(e.latched == 0 && tfw_es_blocking(&e, true));
}
TEST(t_r1_sat_mul_no_wrap)                              /* m-01 */
{
    CHECK(tfw_sat_mul(132u, 65535u) == 8650620u);
    /* 2 MHz / 65535 ticks: 500 ns per tick; past=132 pulses -> old 32-bit product 132*65535*500 wrapped */
    CHECK(tfw_sat_mul(tfw_sat_mul(132u, 65535u), 500u) == UINT32_MAX);
    CHECK((uint32_t)(132u * 65535u * 500u) != UINT32_MAX);                      /* what the old code computed */
    CHECK(tfw_sat_mul(0, 12345) == 0 && tfw_sat_mul(3, 4) == 12);
}
TEST(t_r1_task_mode_reference_period)                   /* M-06 */
{
    CHECK(tfw_upd_ref_us(true, 1000) == 10000u && tfw_upd_ref_us(false, 1000) == 1000u);
    tfw_upd_stats_t s = {0, 0, 0, 0};
    uint32_t cyc = 0;
    for (int i = 0; i < 100; ++i) { tfw_upd_record(&s, cyc, cyc + 400, 400, tfw_upd_ref_us(true, 1000)); cyc += 400u * 10000u; }
    CHECK(s.late == 0);                                   /* exact 10 ms task cadence is not late */
    tfw_upd_stats_t o = {0, 0, 0, 0}; cyc = 0;
    for (int i = 0; i < 100; ++i) { tfw_upd_record(&o, cyc, cyc + 400, 400, 1000); cyc += 400u * 10000u; }
    CHECK(o.late == 99);                                  /* the old reference (cfg_upd_us) counted every call */
    tfw_upd_stats_t l = {0, 0, 0, 0}; cyc = 0;
    for (int i = 0; i < 10; ++i) { tfw_upd_record(&l, cyc, cyc + 400, 400, tfw_upd_ref_us(true, 1000)); cyc += 400u * (i == 5 ? 16000u : 10000u); }
    CHECK(l.late == 1);                                   /* a real 16 ms gap is still detected */
}
TEST(t_r1_pin_conflict)                                 /* M-08 */
{
    const int ax1[3] = {25, 32, 33}, ax2_ok[3] = {2, 3, 4}, ax2_dup[3] = {2, 2, 4}, ax2_unset[3] = {-1, -1, -1};
    const int other[] = {19, -1, -1, -1, 51, 23, 26, 27, 46};
    CHECK(!tfw_pin_conflict(ax1, 3, other, 9));
    CHECK(!tfw_pin_conflict(ax2_ok, 3, other, 9));
    CHECK(tfw_pin_conflict(ax2_dup, 3, other, 9));                              /* duplicate inside the axis */
    CHECK(!tfw_pin_conflict(ax2_unset, 3, other, 9));                           /* unset pins are not a conflict (axis not created) */
    const int hit_estop[3] = {19, 5, 6}, hit_phy[3] = {5, 51, 6}, probe_vs_pls = 2;
    CHECK(tfw_pin_conflict(hit_estop, 3, other, 9));
    CHECK(tfw_pin_conflict(hit_phy, 3, other, 9));
    const int taken[] = {2, 3, 4};
    CHECK(tfw_pin_conflict(&probe_vs_pls, 1, taken, 3));                        /* old default: probe pin 2 == axis 2 pulse pin 2 */
}
/* ===== baseline v0.4 (D-067): shared pulse OE, START edge, reserved pins, servo ON outputs ===== */
TEST(t_v04_oe_level_high_means_allow)                    /* F-03: TFW_PULSE_OE_ACTIVE_LOW = n */
{
    CHECK(tfw_oe_level(true, false) == 1);
    CHECK(tfw_oe_level(false, false) == 0);              /* boot / failure: forbidden = LOW (pull-down state) */
    CHECK(tfw_oe_level(true, true) == 0);
    CHECK(tfw_oe_level(false, true) == 1);
}
TEST(t_v04_servo_on_outputs)                             /* F-09 */
{
    tfw_servo_do_t d = tfw_servo_do(true, true);
    CHECK(d.do1 == 1 && d.do2 == 1 && d.do3 == 0 && d.do4 == 0);
    d = tfw_servo_do(true, false); CHECK(d.do1 == 1 && d.do2 == 0 && d.do3 == 0 && d.do4 == 0);
    d = tfw_servo_do(false, true); CHECK(d.do1 == 0 && d.do2 == 1);
    d = tfw_servo_do(false, false); CHECK(!d.do1 && !d.do2 && !d.do3 && !d.do4);
}
TEST(t_v04_reserved_pins_and_default_axis_pins)          /* F-08, F-01, F-02 */
{
    const int must[] = {17, 18, 19, 54, 23, 26, 27, 46, 24, 47, 48, 33, 51};
    for (unsigned i = 0; i < sizeof must / sizeof must[0]; ++i) {
        int found = 0;
        for (unsigned j = 0; j < TFW_RESERVED_PIN_COUNT; ++j) if (tfw_reserved_pins[j] == must[i]) found = 1;
        CHECK(found);
    }
    const int ax1[2] = {25, 32}, ax2[2] = {20, 21};
    CHECK(!tfw_pin_conflict(ax1, 2, tfw_reserved_pins, TFW_RESERVED_PIN_COUNT));
    CHECK(!tfw_pin_conflict(ax2, 2, tfw_reserved_pins, TFW_RESERVED_PIN_COUNT));
    const int old_en[2] = {25, 33}, dup_di[2] = {20, 18};
    CHECK(tfw_pin_conflict(old_en, 2, tfw_reserved_pins, TFW_RESERVED_PIN_COUNT));   /* 33 is the OE now, not an axis EN */
    CHECK(tfw_pin_conflict(dup_di, 2, tfw_reserved_pins, TFW_RESERVED_PIN_COUNT));
    const int dbg[3] = {15, 16, 22};                                                 /* F-11 candidates are free */
    CHECK(!tfw_pin_conflict(dbg, 3, tfw_reserved_pins, TFW_RESERVED_PIN_COUNT));
}
TEST(t_v04_start_edge_debounced_once)
{
    tfw_start_t s; tfw_start_init(&s, false);
    int edges = 0;
    for (int i = 0; i < 3; ++i) edges += tfw_start_sample(&s, true, false, 1, 5);   /* 3 ms: not yet */
    CHECK(edges == 0);
    for (int i = 0; i < 10; ++i) edges += tfw_start_sample(&s, true, false, 1, 5);
    CHECK(edges == 1);
    for (int i = 0; i < 100; ++i) edges += tfw_start_sample(&s, true, false, 1, 5);  /* level held */
    CHECK(edges == 1);
    for (int i = 0; i < 10; ++i) edges += tfw_start_sample(&s, false, false, 1, 5);  /* falling edge */
    CHECK(edges == 1);
}
TEST(t_v04_start_bounce_is_rejected)
{
    tfw_start_t s; tfw_start_init(&s, false);
    int edges = 0;
    for (int i = 0; i < 50; ++i) edges += tfw_start_sample(&s, (i % 3) == 0, false, 1, 5);   /* chatter < 5 ms */
    CHECK(edges == 0);
}
TEST(t_v04_start_ignored_while_stop_active)
{
    tfw_start_t s; tfw_start_init(&s, false);
    int edges = 0;
    for (int i = 0; i < 20; ++i) edges += tfw_start_sample(&s, true, true, 1, 5);    /* START pressed during STOP */
    CHECK(edges == 0);
    for (int i = 0; i < 20; ++i) edges += tfw_start_sample(&s, true, false, 1, 5);   /* STOP gone, START still held */
    CHECK(edges == 0);                                                               /* the edge was consumed: no restart */
    for (int i = 0; i < 10; ++i) edges += tfw_start_sample(&s, false, false, 1, 5);
    for (int i = 0; i < 10; ++i) edges += tfw_start_sample(&s, true, false, 1, 5);
    CHECK(edges == 1);                                                               /* new edge restarts */
}
TEST(t_v04_start_held_at_boot_is_not_an_edge)
{
    tfw_start_t s; tfw_start_init(&s, true);
    int edges = 0;
    for (int i = 0; i < 50; ++i) edges += tfw_start_sample(&s, true, false, 1, 5);
    CHECK(edges == 0);
}
TEST(t_v04_per_axis_latch_is_independent)                /* tfw_es_t is one instance per axis */
{
    tfw_es_t a, b; tfw_es_init(&a); tfw_es_init(&b);
    tfw_es_trip(&a, 10, 20);
    CHECK(tfw_es_blocking(&a, false) && !tfw_es_blocking(&b, false));
    tfw_es_input_sample(&a, false, 20);
    CHECK(tfw_es_release(&a, false) == 0 && !tfw_es_blocking(&a, false));
    CHECK(!tfw_es_blocking(&b, false));
}
TEST(t_r1_max_clients_register)                         /* C-03 */
{
    tfw_regwin_t w; tfw_results_t r; memset(&r, 0, sizeof r); tfw_regwin_init(&w, 0);
    uint16_t out[1] = {99};
    r.mb_max_clients = 8;
    CHECK(tfw_regwin_read(&w, &r, TFW_R_MB_MAX_CLIENTS, 1, out) == 0 && out[0] == 8);
    uint16_t v = 3; tfw_wr_effect_t fx; memset(&fx, 0, sizeof fx);
    CHECK(tfw_regwin_write(&w, TFW_R_MB_MAX_CLIENTS, 1, &v, &fx) == 2);         /* read-only */
}
TEST(t_r1_seq_aborted_run_leaves_idle_and_short_seq_done)   /* C-01 premise: the device really reports these */
{
    fake_t f; memset(&f, 0, sizeof f); tfw_seq_hw_t h = hw_for(&f);
    tfw_regwin_t w; tfw_regwin_init(&w, 0); static volatile tfw_results_t res; memset((void *)&res, 0, sizeof res);
    w.axis_mask = 3; w.repeat = 1000; w.count = 1000;
    f.abort_flag = true; f.abort_after_ms = 10;
    uint32_t n = tfw_seq_run(&h, &w, &res, TFW_CMD_RUN_SEQ);
    /* the host judge must see an abort through status IDLE and seq_done < repeat (err counters of the earlier,
       completed iterations are 0 and the cut-off iteration counts as one error only if it stopped short) */
    CHECK(res.overshoot[0] == 0 && res.overshoot[1] == 0);
    CHECK(w.status == TFW_ST_IDLE && res.seq_done == n && n < 1000);
}

/* ---- review round 2, N-03: no motion may start after a trip ---- */
TEST(t_r2_seq_no_move_after_trip_between_iterations)
{
    fake_t f; memset(&f, 0, sizeof f); tfw_seq_hw_t h = hw_for(&f);
    tfw_regwin_t w; tfw_regwin_init(&w, 0); static volatile tfw_results_t res; memset((void *)&res, 0, sizeof res);
    w.axis_mask = 3; w.repeat = 5; w.count = 1000; f.trip_when_idle_after_move = true;
    uint32_t n = tfw_seq_run(&h, &w, &res, TFW_CMD_RUN_SEQ);
    CHECK(f.moves[0] == 1 && f.moves[1] == 1);          /* round 2 code started a second iteration with the latch set */
    CHECK(n == 1 && w.status == TFW_ST_IDLE && res.seq_done == 1);
}
TEST(t_r2_seq_no_second_axis_start_after_trip)
{
    fake_t f; memset(&f, 0, sizeof f); tfw_seq_hw_t h = hw_for(&f);
    tfw_regwin_t w; tfw_regwin_init(&w, 0); static volatile tfw_results_t res; memset((void *)&res, 0, sizeof res);
    w.axis_mask = 3; w.repeat = 3; w.count = 1000; f.trip_after_ax0_start = true;
    uint32_t n = tfw_seq_run(&h, &w, &res, TFW_CMD_RUN_SEQ);
    CHECK(f.moves[0] == 1 && f.moves[1] == 0);
    CHECK(f.stops >= 1);                                 /* the axis that had started is stopped */
    CHECK(n == 0 && w.status == TFW_ST_IDLE);
}
TEST(t_r2_seq_start_refused_by_trip_is_abort_not_pulse_error)
{
    fake_t f; memset(&f, 0, sizeof f); tfw_seq_hw_t h = hw_for(&f);
    tfw_regwin_t w; tfw_regwin_init(&w, 0); static volatile tfw_results_t res; memset((void *)&res, 0, sizeof res);
    w.axis_mask = 3; w.repeat = 3; w.count = 1000; f.trip_on_first_move_call = true;
    uint32_t n = tfw_seq_run(&h, &w, &res, TFW_CMD_RUN_SEQ);
    CHECK(res.err[0] == 0 && res.err[1] == 0);
    CHECK(f.moves[0] == 0 && f.moves[1] == 0);
    CHECK(n == 0 && w.status == TFW_ST_IDLE);
}

int main(void)
{
    RUN(t_ticks_never_exceed_request); RUN(t_old_floor_rule_would_exceed); RUN(t_ticks_limits); RUN(t_total_count);
    RUN(t_profile_trapezoid); RUN(t_profile_triangle_and_const); RUN(t_profile_never_above_target); RUN(t_upd_stats);
    RUN(t_judge); RUN(t_random);
    RUN(t_regwin_all_inputs_roundtrip); RUN(t_regwin_out_of_range_sets_0x11); RUN(t_regwin_exceptions);
    RUN(t_regwin_outputs_map); RUN(t_regwin_cfg_flags_b0_is_build_only); RUN(t_regwin_effects);
    RUN(t_regwin_running_ignores_cfg_and_keeps_status); RUN(t_cmd_check);
    RUN(t_seq_1000_iterations_two_axes); RUN(t_seq_counters_consistent_with_injected_faults);
    RUN(t_seq_random_reproducible_by_seed); RUN(t_seq_move_and_fixed_single_axis); RUN(t_seq_abort_and_start_failure);
    RUN(t_estop_latch_rules); RUN(t_estop_blocks_run_commands);
    RUN(t_mb_legacy_read_write); RUN(t_mb_legacy_exceptions); RUN(t_mb_coils_and_di);
    RUN(t_mb_window_125_registers_and_writes); RUN(t_mb_no_persist_side_effect); RUN(t_hist_bins);
    RUN(t_r1_estop_active_at_boot_latches_and_blocks); RUN(t_r1_input_active_blocks_even_when_not_latched);
    RUN(t_r1_sat_mul_no_wrap); RUN(t_r1_task_mode_reference_period); RUN(t_r1_pin_conflict);
    RUN(t_v04_oe_level_high_means_allow); RUN(t_v04_servo_on_outputs); RUN(t_v04_reserved_pins_and_default_axis_pins);
    RUN(t_v04_start_edge_debounced_once); RUN(t_v04_start_bounce_is_rejected); RUN(t_v04_start_ignored_while_stop_active);
    RUN(t_v04_start_held_at_boot_is_not_an_edge); RUN(t_v04_per_axis_latch_is_independent); RUN(t_r1_max_clients_register);
    RUN(t_r1_seq_aborted_run_leaves_idle_and_short_seq_done);
    RUN(t_r2_seq_no_move_after_trip_between_iterations); RUN(t_r2_seq_no_second_axis_start_after_trip);
    RUN(t_r2_seq_start_refused_by_trip_is_abort_not_pulse_error);
    printf("\nchecks=%d failed=%d\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
