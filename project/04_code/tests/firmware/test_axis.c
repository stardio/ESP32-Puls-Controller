/* Host tests of the axis state machine (fw_axis) against the fake HAL: move, profile, stops, STOP latch, START edge,
 * servo ON, init failures. [T-axis-n] */
#include "fake_hal.h"
#include "fw_axis.h"
#include "fw_test.h"

static fw_dev_t D;
static fw_axis_t AX[2];

static void on_start(uint8_t a) { fw_axis_start_physical(&AX[a]); }

static void setup(uint8_t axes)
{
    fake_reset();
    fw_dev_init(&D, axes, false);
    D.boot_status = FW_BOOT_PULSE_X | FW_BOOT_PULSE_Y | FW_BOOT_STOP_IO | FW_BOOT_NVS | FW_BOOT_CFG_OK | FW_BOOT_WDT;
    for (uint8_t a = 0; a < axes; ++a) fw_axis_init(&AX[a], &D, a, 2000000);
    fw_hal_set_start_cb(on_start);
    fw_axis_poll(&AX[0]); if (axes > 1) fw_axis_poll(&AX[1]);
}
static void set_leg(uint32_t pulses, bool fwd, uint32_t speed, uint16_t acc, uint16_t dec)
{
    fw_set32(D.cfg.leg, 0, pulses); D.cfg.leg[2] = fwd ? 1 : 0;
    fw_set32(D.cfg.op[0], FW_O_SPEED, speed); D.cfg.op[0][FW_O_ACCEL] = acc; D.cfg.op[0][FW_O_DECEL] = dec;
}
/* emit pulses in steps of `step`, polling after each */
static void run_until_done(int ax, uint32_t step)
{
    for (int guard = 0; guard < 100000 && fw_axis_running(&AX[ax]); ++guard) { fake_pulses((uint8_t)ax, step); fw_axis_poll(&AX[ax]); }
}

static void test_legacy_move(void)
{
    setup(2);
    set_leg(1000, true, 10000, 100, 100);
    CHECK_EQ(fw_axis_start_legacy(&AX[0]), FW_RES_OK);
    CHECK(FP[0].active);
    CHECK_EQ(FP[0].first_hz, 5000);                                     /* automatic start speed */
    CHECK_EQ(FP[0].target, 1000);
    CHECK(FP[0].dir_high);
    CHECK(FAKE_SERVO[0] && !FAKE_SERVO[1]);                             /* DO1 = servo ON X, automatic with the move */
    fw_axis_poll(&AX[0]);
    CHECK_EQ(D.ax[0].state, FW_AS_POSITIONING);
    CHECK(D.ax[0].status & FW_ASB_BUSY); CHECK(D.ax[0].status & FW_ASB_SERVO);
    CHECK(D.ax[0].moving);
    fake_pulses(0, 500); fw_axis_poll(&AX[0]);
    CHECK_RANGE(FP[0].freq, 5000, 10000);
    CHECK_EQ(D.ax[0].pulses_done, 500);
    CHECK_EQ(D.ax[0].cmd_position, 500);
    run_until_done(0, 37);
    CHECK(!fw_axis_running(&AX[0]));
    CHECK_EQ(D.ax[0].pulses_done, 1000);
    CHECK_EQ(D.ax[0].leg_result, 1); CHECK_EQ(D.ax[0].leg_fault, 0);
    CHECK(D.ax[0].status & FW_ASB_DONE);
    CHECK_EQ(D.ax[0].cmd_position, 1000);
    CHECK_EQ(D.ax[0].last_stop_cause, FW_SC_DONE);
    CHECK_EQ(D.ax[0].move_count, 1);
    CHECK(!FAKE_SERVO[0]);                                              /* servo OFF again */
    CHECK_EQ(D.ax[0].state, FW_AS_IDLE);
    CHECK(FP[0].ended);
    CHECK_EQ(FP[1].starts, 0);                                          /* Y untouched */
}

static void test_reverse_and_dir_invert(void)
{
    setup(2);
    set_leg(300, false, 10000, 100, 100);
    CHECK_EQ(fw_axis_start_legacy(&AX[0]), FW_RES_OK);
    CHECK(!FP[0].dir_high);
    run_until_done(0, 50);
    CHECK_EQ(D.ax[0].cmd_position, -300);                               /* reverse: position decreases */
    D.cfg.par[0][FW_P_DIR_INVERT] = 1;
    set_leg(100, true, 10000, 100, 100);
    CHECK_EQ(fw_axis_start_legacy(&AX[0]), FW_RES_OK);
    CHECK(!FP[0].dir_high);                                             /* dir_invert flips the pin only */
    run_until_done(0, 50);
    CHECK_EQ(D.ax[0].cmd_position, -200);
}

static void test_pulse_mismatch(void)
{
    setup(2);
    set_leg(1000, true, 10000, 100, 100);
    fw_axis_start_legacy(&AX[0]);
    FP[0].finished = true; FP[0].count = 998;                           /* ISR reported done but the counter says 998 */
    fw_axis_poll(&AX[0]);
    CHECK_EQ(D.ax[0].leg_result, 3); CHECK_EQ(D.ax[0].leg_fault, 2);
    CHECK(!(D.ax[0].status & FW_ASB_DONE));
    CHECK_EQ(D.ax[0].pulses_done, 998);
}

static void test_limits_and_rejections(void)
{
    setup(2);
    set_leg(4294967295u, true, 10000, 100, 100);
    CHECK_EQ(fw_axis_start_legacy(&AX[0]), FW_RES_RANGE);               /* above the S32 limit of the axis area */
    CHECK_EQ(D.ax[0].leg_fault, 1);
    CHECK_EQ(FP[0].starts, 0);
    set_leg(2147483647u, true, 10000, 100, 100);
    CHECK_EQ(fw_axis_start_legacy(&AX[0]), FW_RES_OK);                  /* exactly INT32_MAX is fine */
    CHECK_EQ(FP[0].target, 2147483647u);
    CHECK_EQ(fw_axis_start_legacy(&AX[0]), FW_RES_RUNNING);             /* already running */
    fw_axis_command(&AX[0], FW_CMD_QUICK_STOP);
    /* frequency that the timer cannot represent (40 MHz resolution, 100 Hz) */
    fw_axis_init(&AX[0], &D, 0, 40000000);
    set_leg(100, true, 100, 100, 100);
    CHECK_EQ(fw_axis_start_legacy(&AX[0]), FW_RES_RANGE);
    CHECK_EQ(FP[0].starts, 1);                                          /* only the first start */
}

static void test_abs_rel_and_position(void)
{
    setup(2);
    fw_set32(D.cfg.op[0], FW_O_SPEED, 10000); D.cfg.op[0][FW_O_ACCEL] = 100; D.cfg.op[0][FW_O_DECEL] = 100;
    fw_set32(D.cfg.op[0], FW_O_TARGET, 1500);
    CHECK_EQ(fw_axis_command(&AX[0], FW_CMD_START_ABS), FW_RES_POS_INVALID);   /* require_home=1, position unknown */
    CHECK(D.ax[0].cmd_result == FW_RES_POS_INVALID);
    fw_axis_poll(&AX[0]); CHECK(D.ax[0].status & FW_ASB_POS_INVALID);
    fw_set32(D.cfg.op[0], FW_O_TARGET, 500);
    CHECK_EQ(fw_axis_command(&AX[0], FW_CMD_SET_POSITION), FW_RES_OK);
    fw_axis_poll(&AX[0]); CHECK(!(D.ax[0].status & FW_ASB_POS_INVALID)); CHECK_EQ(D.ax[0].cmd_position, 500);
    fw_set32(D.cfg.op[0], FW_O_TARGET, 1500);
    CHECK_EQ(fw_axis_command(&AX[0], FW_CMD_START_ABS), FW_RES_OK);
    CHECK_EQ(FP[0].target, 1000); CHECK(FP[0].dir_high);
    CHECK_EQ(D.ax[0].target_position_act, 1500);
    run_until_done(0, 100);
    CHECK_EQ(D.ax[0].cmd_position, 1500);
    CHECK_EQ(fw_axis_command(&AX[0], FW_CMD_START_ABS), FW_RES_OK);     /* already at the target: completes at once */
    CHECK(!fw_axis_running(&AX[0])); CHECK_EQ(D.ax[0].leg_result, 1);
    fw_set32(D.cfg.op[0], FW_O_TARGET, 0);
    CHECK_EQ(fw_axis_command(&AX[0], FW_CMD_START_ABS), FW_RES_OK);     /* 1500 -> 0 backwards */
    CHECK_EQ(FP[0].target, 1500); CHECK(!FP[0].dir_high);
    run_until_done(0, 100);
    CHECK_EQ(D.ax[0].cmd_position, 0);
    CHECK_EQ(fw_axis_command(&AX[0], FW_CMD_START_REL), FW_RES_RANGE);  /* relative 0 pulses */
    fw_set32(D.cfg.op[0], FW_O_TARGET, (uint32_t)-300);
    CHECK_EQ(fw_axis_command(&AX[0], FW_CMD_START_REL), FW_RES_OK);     /* negative = reverse */
    CHECK_EQ(FP[0].target, 300); CHECK(!FP[0].dir_high);
    run_until_done(0, 100);
    CHECK_EQ(D.ax[0].cmd_position, -300);
    D.cfg.par[0][FW_P_REQUIRE_HOME] = 0;                                /* with require_home off an unknown position is accepted */
    fw_axis_init(&AX[0], &D, 0, 2000000);
    fw_set32(D.cfg.op[0], FW_O_TARGET, 10);
    CHECK_EQ(fw_axis_command(&AX[0], FW_CMD_START_ABS), FW_RES_OK);
}

/* [T-axis-stop] HMI stop decelerates; QUICK_STOP is immediate */
static void test_decel_stop(void)
{
    setup(2);
    set_leg(100000, true, 100000, 100, 100);
    fw_axis_start_legacy(&AX[0]);
    for (int i = 0; i < 40; ++i) { fake_pulses(0, 500); fw_axis_poll(&AX[0]); }     /* 20,000 pulses: accelerating/cruising */
    const uint32_t n_at_stop = FP[0].count, f_at_stop = FP[0].freq;
    CHECK(f_at_stop > 50000);
    CHECK_EQ(fw_axis_command(&AX[0], FW_CMD_STOP), FW_RES_OK);
    fw_axis_poll(&AX[0]);
    CHECK_EQ(D.ax[0].state, FW_AS_DECEL_STOP);
    uint32_t prev = FP[0].freq;
    for (int i = 0; i < 2000 && fw_axis_running(&AX[0]); ++i) {
        fake_pulses(0, FP[0].count + 20000 < 100000 && FP[0].freq > 20000 ? 200 : 20); fw_axis_poll(&AX[0]);
        if (fw_axis_running(&AX[0])) { CHECK(FP[0].freq <= prev + 1); prev = FP[0].freq; }
    }
    CHECK(!fw_axis_running(&AX[0]));
    CHECK(FP[0].count > n_at_stop);                                     /* it decelerated over some distance */
    CHECK(FP[0].count < 100000);                                        /* and did not run to the target */
    CHECK_EQ(D.ax[0].leg_result, 2); CHECK_EQ(D.ax[0].leg_fault, 0);
    CHECK_EQ(D.ax[0].last_stop_cause, FW_SC_STOP_CMD);
    CHECK_EQ(D.ax[0].pulses_done, FP[0].count);
    CHECK(!FAKE_SERVO[0]);
    CHECK_EQ(D.ax[0].cmd_position, (int32_t)FP[0].count);
    CHECK(FP[0].freq <= 12000);                                         /* ended near the start speed (20-pulse steps) */
    /* stop of an idle axis is harmless */
    CHECK_EQ(fw_axis_command(&AX[0], FW_CMD_STOP), FW_RES_OK);
}

static void test_quick_stop(void)
{
    setup(2);
    set_leg(100000, true, 100000, 100, 100);
    fw_axis_start_legacy(&AX[0]);
    fake_pulses(0, 3000); fw_axis_poll(&AX[0]);
    CHECK_EQ(fw_axis_command(&AX[0], FW_CMD_QUICK_STOP), FW_RES_OK);
    CHECK(!fw_axis_running(&AX[0]));
    CHECK(FP[0].stops >= 1 && FP[0].ended);
    CHECK_EQ(D.ax[0].pulses_done, 3000);
    CHECK_EQ(D.ax[0].leg_result, 2);
    CHECK_EQ(D.ax[0].last_stop_cause, FW_SC_QUICK);
    CHECK(!FAKE_SERVO[0]);
    fw_axis_legacy_reset(&AX[0]);
    CHECK_EQ(D.ax[0].pulses_done, 0); CHECK_EQ(D.ax[0].leg_result, 0);
}

/* [T-stop-axis] STOP of one axis cuts only that axis; restart only by a START edge after release (D-058) */
static void test_stop_latch_and_start_edge(void)
{
    setup(2);
    set_leg(100000, true, 100000, 100, 100);
    fw_set32(D.cfg.op[1], FW_O_SPEED, 20000); fw_set32(D.cfg.op[1], FW_O_TARGET, 50000);
    fw_axis_start_legacy(&AX[0]);
    CHECK_EQ(fw_axis_command(&AX[1], FW_CMD_START_REL), FW_RES_OK);
    CHECK(FAKE_SERVO[1]);
    fake_pulses(0, 2000); fake_pulses(1, 2000);
    fw_axis_poll(&AX[0]); fw_axis_poll(&AX[1]);
    fake_io_ms(0, 3, true, false);                                      /* X STOP opens (ISR cuts the output) */
    CHECK(!FP[0].active); CHECK(FP[1].active);                          /* Y keeps running */
    fw_axis_poll(&AX[0]); fw_axis_poll(&AX[1]);
    CHECK(!fw_axis_running(&AX[0])); CHECK(fw_axis_running(&AX[1]));
    CHECK_EQ(D.ax[0].state, FW_AS_ESTOP); CHECK_EQ(D.ax[0].alarm_code, FW_AL_ESTOP);
    CHECK(D.ax[0].status & FW_ASB_ESTOP); CHECK(D.ax[0].status & FW_ASB_ALARM); CHECK(D.ax[0].status & FW_ASB_POS_INVALID);
    CHECK_EQ(D.ax[0].pulses_done, 2000);                                /* the pulses that really went out */
    CHECK_EQ(D.ax[0].leg_result, 2); CHECK_EQ(D.ax[0].leg_fault, 1);
    CHECK_EQ(D.ax[1].state, FW_AS_POSITIONING);
    CHECK_EQ(D.ax[1].alarm_code, FW_AL_NONE);
    CHECK(!FAKE_SERVO[0]); CHECK(FAKE_SERVO[1]);
    /* while STOP is active or latched every start is refused */
    CHECK_EQ(fw_axis_start_legacy(&AX[0]), FW_RES_ESTOP);
    CHECK_EQ(fw_axis_command(&AX[0], FW_CMD_ALARM_RESET), FW_RES_ESTOP);   /* no reset command for the latch */
    fake_io_ms(0, 20, true, true);                                      /* START pressed while STOP is active: ignored */
    CHECK_EQ(FAKE_START_CB_N[0], 0);
    fake_io_ms(0, 50, false, true);                                     /* STOP released, START still held: still latched */
    CHECK_EQ(FAKE_START_CB_N[0], 0);
    fw_axis_poll(&AX[0]);
    CHECK_EQ(D.ax[0].state, FW_AS_ESTOP);                               /* released but latched */
    CHECK_EQ(fw_axis_start_legacy(&AX[0]), FW_RES_ESTOP);               /* the HMI cannot restart either */
    CHECK(!fw_axis_running(&AX[0]));
    fake_io_ms(0, 10, false, false);
    fake_io_ms(0, 10, false, true);                                     /* START edge: releases and starts (callback) */
    CHECK_EQ(FAKE_START_CB_N[0], 1);
    CHECK(fw_axis_running(&AX[0]));
    CHECK(FP[0].active);
    fw_axis_poll(&AX[0]);
    CHECK_EQ(D.ax[0].alarm_code, FW_AL_NONE);
    CHECK_EQ(D.ax[0].state, FW_AS_POSITIONING);
    CHECK(!(D.ax[0].status & FW_ASB_ESTOP));
    CHECK(D.ax[0].status & FW_ASB_POS_INVALID);                         /* position stays invalid until SET_POSITION / HOME */
}

static void test_stop_active_at_boot(void)
{
    fake_reset();
    fake_stop_isr(0);
    fw_dev_init(&D, 2, false);
    D.boot_status = FW_BOOT_PULSE_X | FW_BOOT_PULSE_Y | FW_BOOT_STOP_IO | FW_BOOT_NVS | FW_BOOT_CFG_OK | FW_BOOT_WDT;
    fw_axis_init(&AX[0], &D, 0, 2000000); fw_axis_init(&AX[1], &D, 1, 2000000);
    fw_axis_poll(&AX[0]); fw_axis_poll(&AX[1]);
    CHECK_EQ(D.ax[0].state, FW_AS_ESTOP);
    CHECK_EQ(D.ax[1].state, FW_AS_IDLE);
    CHECK_EQ(fw_axis_start_legacy(&AX[0]), FW_RES_ESTOP);
    CHECK_EQ(FP[0].starts, 0);
}

static void test_start_physical_y(void)
{
    setup(2);
    fw_set32(D.cfg.op[1], FW_O_SPEED, 20000); fw_set32(D.cfg.op[1], FW_O_TARGET, (uint32_t)-4000);
    fake_io_ms(1, 10, false, true);                                     /* Y START edge: START_REL by target_position */
    CHECK_EQ(FAKE_START_CB_N[1], 1);
    CHECK(FP[1].active); CHECK_EQ(FP[1].target, 4000); CHECK(!FP[1].dir_high);
    CHECK_EQ(FP[0].starts, 0);
    CHECK(FAKE_SERVO[1] && !FAKE_SERVO[0]);                             /* DO2 for Y */
}

static void test_servo_modes(void)
{
    setup(2);
    set_leg(1000, true, 10000, 100, 100);
    D.cfg.par[0][FW_P_SVON_MODE] = 1;
    fw_axis_poll(&AX[0]);
    CHECK_EQ(D.ax[0].state, FW_AS_SERVO_OFF);
    CHECK_EQ(fw_axis_start_legacy(&AX[0]), FW_RES_SERVO_OFF);
    CHECK_EQ(fw_axis_command(&AX[0], FW_CMD_SERVO_ON), FW_RES_OK);
    CHECK(FAKE_SERVO[0]);
    CHECK_EQ(fw_axis_start_legacy(&AX[0]), FW_RES_OK);
    CHECK_EQ(fw_axis_command(&AX[0], FW_CMD_SERVO_OFF), FW_RES_RUNNING);
    run_until_done(0, 100);
    CHECK(FAKE_SERVO[0]);                                               /* manual servo stays on after the move */
    CHECK_EQ(fw_axis_command(&AX[0], FW_CMD_SERVO_OFF), FW_RES_OK);
    CHECK(!FAKE_SERVO[0]);
    D.cfg.par[0][FW_P_SVON_MODE] = 0;                                   /* automatic: manual ON is still honoured */
    fw_axis_command(&AX[0], FW_CMD_SERVO_ON);
    CHECK(FAKE_SERVO[0]);
    fw_axis_command(&AX[0], FW_CMD_SERVO_OFF);
    D.cfg.par[1][FW_P_SVON_AT_BOOT] = 1;
    fw_axis_servo_boot(&AX[1]);
    CHECK(FAKE_SERVO[1]);
}

/* [T-init] initialisation failures prohibit operation */
static void test_init_failures(void)
{
    setup(2);
    set_leg(1000, true, 10000, 100, 100);
    FP[0].ready = false;                                                /* pulse engine X not created */
    fw_axis_poll(&AX[0]);
    CHECK_EQ(D.ax[0].alarm_code, FW_AL_PE_HW);
    CHECK_EQ(fw_axis_start_legacy(&AX[0]), FW_RES_ALARM);
    CHECK(!(D.ax[0].status & FW_ASB_READY));
    FP[0].ready = true; fw_axis_poll(&AX[0]);
    CHECK_EQ(D.ax[0].alarm_code, FW_AL_NONE);
    CHECK_EQ(fw_axis_start_legacy(&AX[0]), FW_RES_OK);
    fw_axis_command(&AX[0], FW_CMD_QUICK_STOP);

    D.boot_status &= (uint16_t)~FW_BOOT_CFG_OK;                         /* corrupt stored configuration */
    fw_axis_poll(&AX[0]); fw_axis_poll(&AX[1]);
    CHECK_EQ(D.ax[0].alarm_code, FW_AL_PARAM); CHECK_EQ(D.ax[1].alarm_code, FW_AL_PARAM);
    CHECK_EQ(fw_axis_start_legacy(&AX[0]), FW_RES_ALARM);
    D.boot_status |= FW_BOOT_CFG_OK;                                    /* SAVE done */
    fw_axis_poll(&AX[0]);
    CHECK_EQ(D.ax[0].alarm_code, FW_AL_NONE);
    CHECK_EQ(fw_axis_start_legacy(&AX[0]), FW_RES_OK);
    fw_axis_command(&AX[0], FW_CMD_QUICK_STOP);

    D.boot_status &= (uint16_t)~FW_BOOT_STOP_IO;                        /* STOP/START init failed */
    CHECK_EQ(fw_axis_start_legacy(&AX[0]), FW_RES_ALARM);
    D.boot_status |= FW_BOOT_STOP_IO; D.boot_status &= (uint16_t)~FW_BOOT_WDT;
    CHECK_EQ(fw_axis_start_legacy(&AX[0]), FW_RES_ALARM);               /* no watchdog: no operation */
    D.boot_status |= FW_BOOT_WDT; D.boot_status &= (uint16_t)~FW_BOOT_NVS;
    CHECK_EQ(fw_axis_start_legacy(&AX[0]), FW_RES_ALARM);
    D.boot_status |= FW_BOOT_NVS;
    FAKE_IO_READY = false;                                              /* HAL io not ready: blocking */
    CHECK_EQ(fw_axis_start_legacy(&AX[0]), FW_RES_ESTOP);
    FAKE_IO_READY = true;
}

static void test_commloss_stop_and_alarm_reset(void)
{
    setup(2);
    set_leg(100000, true, 100000, 100, 100);
    fw_axis_start_legacy(&AX[0]);
    for (int i = 0; i < 20; ++i) { fake_pulses(0, 500); fw_axis_poll(&AX[0]); }
    fw_axis_decel_all_reason(&AX[0], FW_AL_COMMLOSS);
    fw_axis_poll(&AX[0]);
    CHECK_EQ(D.ax[0].state, FW_AS_DECEL_STOP);
    CHECK_EQ(D.ax[0].alarm_code, FW_AL_COMMLOSS);
    for (int i = 0; i < 2000 && fw_axis_running(&AX[0]); ++i) { fake_pulses(0, 200); fw_axis_poll(&AX[0]); }
    CHECK(!fw_axis_running(&AX[0]));
    CHECK_EQ(D.ax[0].state, FW_AS_ALARM);
    CHECK_EQ(fw_axis_start_legacy(&AX[0]), FW_RES_ALARM);               /* alarm stop: no start until reset */
    CHECK_EQ(fw_axis_command(&AX[0], FW_CMD_ALARM_RESET), FW_RES_OK);
    fw_axis_poll(&AX[0]);
    CHECK_EQ(D.ax[0].alarm_code, FW_AL_NONE);
    CHECK_EQ(D.hist_count, 1); CHECK_EQ(D.hist[0].code, FW_AL_COMMLOSS);
    CHECK_EQ(fw_axis_start_legacy(&AX[0]), FW_RES_OK);
}

static void test_unsupported_and_profile_failure(void)
{
    setup(2);
    CHECK_EQ(fw_axis_command(&AX[0], FW_CMD_HOME), FW_RES_RANGE);
    CHECK_EQ(fw_axis_command(&AX[0], FW_CMD_JOG_POS), FW_RES_RANGE);
    set_leg(100000, true, 100000, 100, 100);
    fw_axis_start_legacy(&AX[0]);
    fake_pulses(0, 1000);
    FP[0].fail_set_freq = true;
    fw_axis_poll(&AX[0]);                                               /* period update refused by the HAL */
    CHECK(!fw_axis_running(&AX[0]));
    CHECK_EQ(D.ax[0].leg_result, 3); CHECK_EQ(D.ax[0].leg_fault, 3);    /* legacy fault 3 = profile update error */
    FP[0].fail_set_freq = false;
}

static void test_one_axis_build(void)
{
    setup(1);
    set_leg(500, true, 10000, 100, 100);
    CHECK_EQ(fw_axis_start_legacy(&AX[0]), FW_RES_OK);
    run_until_done(0, 100);
    CHECK_EQ(D.ax[0].leg_result, 1);
    CHECK_EQ(D.axis_count, 1);
    CHECK_EQ(FP[1].starts, 0);
}

int main(void)
{
    RUN(test_legacy_move); RUN(test_reverse_and_dir_invert); RUN(test_pulse_mismatch); RUN(test_limits_and_rejections);
    RUN(test_abs_rel_and_position); RUN(test_decel_stop); RUN(test_quick_stop); RUN(test_stop_latch_and_start_edge);
    RUN(test_stop_active_at_boot); RUN(test_start_physical_y); RUN(test_servo_modes); RUN(test_init_failures);
    RUN(test_commloss_stop_and_alarm_reset); RUN(test_unsupported_and_profile_failure); RUN(test_one_axis_build);
    SUMMARY();
}
