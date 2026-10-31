/* Host tests of the register map / Modbus PDU layer (fw_regmap): encoding, legacy window compatibility, axis areas,
 * exceptions, atomic writes, command routing. [T-reg-n] */
#include "fw_test.h"
#include "fw_regmap.h"

typedef struct { int axis_cmd_n, legacy_start_n, legacy_reset_n, sys_cmd_n, user_do_n, cfg_changed_n;
                 int last_axis, last_cmd, last_sys, last_do; } rec_t;
static rec_t R;
static uint8_t op_axis_cmd(void *c, uint8_t a, uint16_t cmd) { (void)c; R.axis_cmd_n++; R.last_axis = a; R.last_cmd = cmd; return 0; }
static uint8_t op_legacy_start(void *c) { (void)c; R.legacy_start_n++; return 0; }
static void op_legacy_reset(void *c) { (void)c; R.legacy_reset_n++; }
static uint16_t op_sys_cmd(void *c, uint16_t cmd) { (void)c; R.sys_cmd_n++; R.last_sys = cmd; return 0; }
static void op_user_do(void *c, uint16_t m) { (void)c; R.user_do_n++; R.last_do = m; }
static void op_cfg_changed(void *c) { (void)c; R.cfg_changed_n++; }

static fw_dev_t D;
static void setup(uint8_t axes, bool rtu)
{
    memset(&R, 0, sizeof(R));
    fw_dev_init(&D, axes, rtu);
    D.ops = (fw_dev_ops_t){op_axis_cmd, op_legacy_start, op_legacy_reset, op_sys_cmd, op_user_do, op_cfg_changed, NULL};
    D.fw_version = (1u << 16) | (2u << 8) | 3u;
}

static uint8_t RESP[300];
static size_t call(const uint8_t *req, size_t n) { return fw_mb_process(&D, req, n, RESP); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
/* returns 0 on success (RESP filled), else the exception code */
static int rd(uint8_t fc, unsigned addr, unsigned n)
{
    uint8_t q[5] = {fc, (uint8_t)(addr >> 8), (uint8_t)addr, (uint8_t)(n >> 8), (uint8_t)n};
    size_t l = call(q, 5);
    return (RESP[0] & 0x80) ? RESP[1] : (l ? 0 : 99);
}
static uint16_t reg(unsigned addr) { CHECK_EQ(rd(3, addr, 1), 0); return rd16(RESP + 2); }
static uint32_t reg32(unsigned addr) { CHECK_EQ(rd(3, addr, 2), 0); return ((uint32_t)rd16(RESP + 4) << 16) | rd16(RESP + 2); }
static int wr(unsigned addr, const uint16_t *v, unsigned n)
{
    uint8_t q[260]; q[0] = 16; q[1] = (uint8_t)(addr >> 8); q[2] = (uint8_t)addr; q[3] = (uint8_t)(n >> 8); q[4] = (uint8_t)n; q[5] = (uint8_t)(2 * n);
    for (unsigned i = 0; i < n; ++i) { q[6 + 2 * i] = (uint8_t)(v[i] >> 8); q[7 + 2 * i] = (uint8_t)v[i]; }
    call(q, 6 + 2 * n);
    return (RESP[0] & 0x80) ? RESP[1] : 0;
}
static int wr1(unsigned addr, uint16_t v)
{
    uint8_t q[5] = {6, (uint8_t)(addr >> 8), (uint8_t)addr, (uint8_t)(v >> 8), (uint8_t)v};
    call(q, 5);
    return (RESP[0] & 0x80) ? RESP[1] : 0;
}
static int wr32(unsigned addr, uint32_t v) { uint16_t w[2] = {(uint16_t)v, (uint16_t)(v >> 16)}; return wr(addr, w, 2); }
static int coil_w(unsigned addr, bool on)
{
    uint8_t q[5] = {5, (uint8_t)(addr >> 8), (uint8_t)addr, on ? 0xFF : 0, 0};
    call(q, 5);
    return (RESP[0] & 0x80) ? RESP[1] : 0;
}
static int coil_r(unsigned addr, unsigned n)
{
    uint8_t q[5] = {1, (uint8_t)(addr >> 8), (uint8_t)addr, (uint8_t)(n >> 8), (uint8_t)n};
    call(q, 5);
    return (RESP[0] & 0x80) ? -RESP[1] : RESP[2 + 0];
}

/* ---------------------------------------------------------------- legacy window (servo1axis compatible) */
static void test_legacy_read(void)
{
    setup(2, false);
    CHECK_EQ(rd(3, 0, 4), 0);                                           /* HR0:1 position, HR2:3 speed */
    CHECK_EQ(RESP[1], 8);
    CHECK_EQ(rd16(RESP + 2), 5000); CHECK_EQ(rd16(RESP + 4), 0);        /* low word first */
    CHECK_EQ(rd16(RESP + 6), 10000); CHECK_EQ(rd16(RESP + 8), 0);
    CHECK_EQ(reg32(0x12), 5000);                                        /* start speed = min(5000, speed) */
    CHECK_EQ(reg32(0x14), 400); CHECK_EQ(reg32(0x16), 400);             /* accel / decel */
    CHECK_EQ(reg(0x08), 0); CHECK_EQ(reg(0x1F), 0);                     /* reserved: read 0 */
    CHECK_EQ(rd(3, 0, 0x20), 0);                                        /* whole window readable in one request */
    CHECK_EQ(rd(3, 0x1F, 2), 0);                                        /* crossing into the system area */
    D.link = true; D.tcp_clients = 2;
    CHECK_EQ(reg(0x10), 1); CHECK_EQ(reg(0x0F), 2);
    D.ax[0].moving = true; D.ax[0].leg_result = 1; D.ax[0].servo_manual = true;
    CHECK_EQ(reg(0x0A), 0x1F);                                          /* b0 link, b1 client, b2 servo, b3 moving, b4 ok */
    D.ax[0].leg_result = 3; D.ax[0].leg_fault = 2;
    CHECK_EQ(reg(0x0D), 3); CHECK_EQ(reg(0x0E), 2); CHECK_EQ(reg(0x0A) & 0x20, 0x20);
    D.ax[0].pulses_done = 0x00012345u;
    CHECK_EQ(reg32(6), 0x12345);
}

static void test_legacy_write(void)
{
    setup(2, false);
    CHECK_EQ(wr32(0, 4294967295u), 0);                                  /* U32 full range accepted in the legacy window */
    CHECK_EQ(reg32(0), 4294967295u);
    CHECK_EQ(wr32(0, 0), 3);                                            /* 0 pulses: illegal value */
    CHECK_EQ(reg32(0), 4294967295u);                                    /* unchanged */
    CHECK_EQ(wr32(2, 1000000), 0); CHECK_EQ(wr32(2, 1000001), 3); CHECK_EQ(wr32(2, 99), 3);
    CHECK_EQ(wr32(0x14, 10), 0); CHECK_EQ(wr32(0x14, 9), 3); CHECK_EQ(wr32(0x14, 60001), 3);
    CHECK_EQ(reg32(0x14), 10);
    CHECK_EQ(wr32(0x16, 60000), 0);
    CHECK_EQ(wr1(4, 1), 2);                                             /* HR4: unused -> exception 02 */
    CHECK_EQ(wr1(5, 0), 2);
    CHECK_EQ(wr32(0x12, 100), 2);                                       /* start speed read-only */
    CHECK_EQ(wr1(6, 0), 2); CHECK_EQ(wr1(0x0A, 0), 2); CHECK_EQ(wr1(0x0D, 0), 2); CHECK_EQ(wr1(0x1F, 0), 2);
    /* half-word writes are legal in the legacy window (servo1axis behaviour) */
    CHECK_EQ(wr1(2, 0x4240), 0); CHECK_EQ(wr1(3, 0x000F), 0);           /* 0x000F4240 = 1,000,000 */
    CHECK_EQ(reg32(2), 1000000);
    CHECK_EQ(wr1(3, 0x0010), 3);
    CHECK_EQ(reg32(2), 1000000);
    CHECK_EQ(wr1(0x15, 1), 3);                                          /* high word of the 16-bit accel must stay 0 */
    /* the legacy speed/accel are the X axis registers */
    CHECK_EQ(reg32(0x1044), 1000000); CHECK_EQ(reg(0x1046), 10); CHECK_EQ(reg(0x1047), 60000);
}

static void test_legacy_coils(void)
{
    setup(2, false);
    CHECK_EQ(coil_r(0, 8), 0x08);                                       /* direction ON by default, others OFF */
    CHECK_EQ(coil_w(0, true), 0); CHECK_EQ(R.legacy_start_n, 1);
    CHECK_EQ(coil_w(0, false), 0); CHECK_EQ(R.legacy_start_n, 1);
    CHECK_EQ(coil_r(0, 1), 0);                                          /* START is momentary */
    CHECK_EQ(coil_w(2, true), 0); CHECK_EQ(R.last_cmd, FW_CMD_QUICK_STOP); CHECK_EQ(R.legacy_reset_n, 1);
    CHECK_EQ(coil_w(1, true), 0); CHECK_EQ(R.last_cmd, FW_CMD_SERVO_ON);
    CHECK_EQ(coil_w(1, false), 0); CHECK_EQ(R.last_cmd, FW_CMD_SERVO_OFF);
    int before = R.cfg_changed_n;
    CHECK_EQ(coil_w(3, false), 0);
    CHECK_EQ(R.cfg_changed_n, before + 1);                              /* direction is persistent */
    CHECK_EQ(coil_r(3, 1), 0); CHECK_EQ(D.cfg.leg[2] & 1, 0);
    CHECK_EQ(coil_w(3, false), 0); CHECK_EQ(R.cfg_changed_n, before + 1);   /* unchanged: no save request */
    CHECK_EQ(coil_w(6, true), 0);                                       /* coils 4..7: accepted, no function */
    CHECK_EQ(coil_r(6, 1), 1);
    CHECK_EQ(coil_w(8, true), 2);                                       /* 8..0xF undefined */
    /* FC15 */
    uint8_t q[] = {15, 0, 0, 0, 4, 1, 0x0A};                            /* coils 0..3 = 0,1,0,1 */
    call(q, sizeof(q));
    CHECK_EQ(RESP[0], 15); CHECK_EQ(R.last_cmd, FW_CMD_SERVO_ON); CHECK_EQ(coil_r(3, 1), 1);
    /* FC02: DI 0..4 = running, link, client, complete, fault */
    D.ax[0].moving = true; D.link = true; D.ax[0].leg_fault = 1;
    uint8_t d[] = {2, 0, 0, 0, 8};
    call(d, 5);
    CHECK_EQ(RESP[2], 0x13);
    uint8_t d2[] = {2, 0, 0, 0, 9};
    call(d2, 5); CHECK_EQ(RESP[0], 0x82); CHECK_EQ(RESP[1], 2);
}

/* ---------------------------------------------------------------- system area */
static void test_system_area(void)
{
    setup(2, true);
    D.reset_reason = FW_RST_TASK_WDT; D.boot_status = 0x021F; D.uptime_s = 0x00012345u; D.heartbeat = 77;
    D.comm_mode_active = FW_MODE_RTU; D.sys_status = 0x0005; D.estop_state = 2; D.alarm_axis_mask = 2;
    D.di_raw = 0x00A5; D.do_state = 0x0009; D.tcp_clients = 3; D.sys_inputs = 3;
    CHECK_EQ(reg(0x20), 2); CHECK_EQ(reg(0x21), 2);
    CHECK_EQ(reg32(0x22), 0x00010203u); CHECK_EQ(reg(0x22), 0x0203);    /* low word first */
    CHECK_EQ(reg(0x24), 2);                                             /* regmap_version */
    CHECK_EQ(reg(0x25), 5); CHECK_EQ(reg(0x26), 2); CHECK_EQ(reg(0x27), 3);
    CHECK_EQ(reg32(0x28), 0x12345); CHECK_EQ(reg(0x2A), 77); CHECK_EQ(reg(0x2B), 3);
    CHECK_EQ(reg(0x2C), FW_RST_TASK_WDT);                               /* reset cause for T-15 */
    CHECK_EQ(reg(0x2D), 0x021F); CHECK_EQ(reg(0x2E), FW_MODE_RTU);
    CHECK_EQ(reg(0x30), 2); CHECK_EQ(reg(0x31), 0xA5); CHECK_EQ(reg(0x32), 9);
    CHECK_EQ(rd(3, 0x20, 0x30), 0);                                     /* a block read over holes works (holes read 0) */
    CHECK_EQ(reg(0x29), 1);                                             /* uptime high word */
    CHECK_EQ(wr1(0x20, 1), 2); CHECK_EQ(wr1(0x2C, 0), 2); CHECK_EQ(wr1(0x31, 0), 2);   /* read-only */
    D.model_rtu = false; setup(1, false); CHECK_EQ(reg(0x20), 1); CHECK_EQ(reg(0x21), 1);
}

static void test_system_cfg_and_commands(void)
{
    setup(2, true);
    CHECK_EQ(reg(0x110), 0); CHECK_EQ(reg(0x111), 1000); CHECK_EQ(reg(0x112), 0); CHECK_EQ(reg(0x113), 1); CHECK_EQ(reg(0x114), 4);
    CHECK_EQ(wr1(0x111, 99), 3); CHECK_EQ(wr1(0x111, 100), 0); CHECK_EQ(wr1(0x114, 9), 3); CHECK_EQ(wr1(0x114, 8), 0);
    CHECK_EQ(wr1(0x112, 2), 3); CHECK_EQ(wr1(0x110, 2), 3);
    CHECK_EQ(wr1(0x100, FW_SYS_SAVE), 0); CHECK_EQ(R.last_sys, FW_SYS_SAVE);
    CHECK_EQ(wr1(0x100, FW_SYS_STOP_ALL), 0); CHECK_EQ(R.last_sys, FW_SYS_STOP_ALL);
    CHECK_EQ(wr1(0x100, 7), 3); CHECK_EQ(wr1(0x100, 0x100), 3);        /* 0x100 is internal, not writable here */
    CHECK_EQ(reg(0x100), 0);                                            /* sys_command reads 0 */
    CHECK_EQ(wr1(0x101, 0), 2);                                         /* sys_cmd_result read-only */
    /* unlock key with a 10 s window */
    D.now_ms = 1000;
    CHECK(!fw_dev_unlocked(&D));
    CHECK_EQ(wr1(0x102, 0x1234), 3); CHECK(!fw_dev_unlocked(&D));
    CHECK_EQ(wr1(0x102, FW_UNLOCK_KEY), 0); CHECK(fw_dev_unlocked(&D));
    D.now_ms = 1000 + FW_UNLOCK_MS - 1; CHECK(fw_dev_unlocked(&D));
    D.now_ms = 1000 + FW_UNLOCK_MS; CHECK(!fw_dev_unlocked(&D));
    /* user outputs DO3/DO4 */
    CHECK_EQ(wr1(0x120, 3), 0); CHECK_EQ(R.user_do_n, 1); CHECK_EQ(R.last_do, 3); CHECK_EQ(reg(0x120), 3);
    CHECK_EQ(wr1(0x120, 4), 3); CHECK_EQ(reg(0x120), 3);
    /* sys result register */
    D.sys_cmd_result = 0x11; CHECK_EQ(reg(0x101), 0x11);
}

/* ---------------------------------------------------------------- axis areas */
static void test_axis_area_layout(void)
{
    setup(2, false);
    D.ax[0].state = 2; D.ax[0].status = 0x0402; D.ax[0].cmd_position = -2; D.ax[0].cmd_speed = -5000;
    D.ax[0].target_position_act = 123456; D.ax[0].alarm_code = 0x0101; D.ax[0].last_stop_cause = 3; D.ax[0].cmd_result = 0x10;
    D.ax[0].move_count = 0x10002; D.ax[0].pulses_done = 99;
    D.ax[1].state = 5;
    CHECK_EQ(reg(0x1000), 2); CHECK_EQ(reg(0x1001), 0x0402);
    CHECK_EQ(reg(0x1002), 0xFFFE); CHECK_EQ(reg(0x1003), 0xFFFF);        /* S32 -2, low word first */
    CHECK_EQ((int32_t)reg32(0x1004), -5000);
    CHECK_EQ(reg32(0x1006), 123456);
    CHECK_EQ(reg(0x1008), 0x0101); CHECK_EQ(reg(0x1009), 3); CHECK_EQ(reg(0x100B), 0x10);
    CHECK_EQ(reg32(0x100C), 0x10002); CHECK_EQ(reg32(0x100E), 99);
    CHECK_EQ(reg(0x1200), 0x0101);                                      /* alarm_code mirror in the diagnostic block */
    CHECK_EQ(reg(0x2000), 5);                                           /* axis 2 is a separate area */
    CHECK_EQ(reg(0x1040), 0);                                           /* axis_command reads 0 */
    CHECK_EQ(wr1(0x1000, 0), 2); CHECK_EQ(wr1(0x1008, 0), 2);           /* read-only */
    CHECK_EQ(rd(3, 0x1000, 0x10), 0);
    CHECK_EQ(rd(3, 0x1200, 8), 0);
    CHECK_EQ(rd(3, 0x1080, 1), 2);                                      /* between the blocks: undefined */
    CHECK_EQ(rd(3, 0x1300, 1), 2);
    CHECK_EQ(rd(3, 0x3000 + 0x100, 1), 2);
    CHECK_EQ(rd(3, 0x5000, 1), 2);                                      /* firmware-update window: not in this firmware */
    CHECK_EQ(rd(3, 0x6000, 1), 2);
    CHECK_EQ(rd(4, 0x1000, 1), 0);                                      /* FC04 answers like FC03 */
}

static void test_axis_op_and_params(void)
{
    setup(2, false);
    CHECK_EQ(wr32(0x1042, (uint32_t)-1000), 0); CHECK_EQ((int32_t)reg32(0x1042), -1000);   /* target_position S32 */
    CHECK_EQ(wr32(0x2042, 777), 0); CHECK_EQ(reg32(0x2042), 777);
    CHECK_EQ((int32_t)reg32(0x1042), -1000);                            /* axes independent */
    CHECK_EQ(wr1(0x1044, 5000), 3);                                     /* 32-bit item by one half: exception 03 */
    CHECK_EQ(wr32(0x1044, 200000), 3);                                  /* above max_speed (100,000) in the axis area */
    CHECK_EQ(wr32(0x1044, 100000), 0);
    CHECK_EQ(wr1(0x1046, 9), 3); CHECK_EQ(wr1(0x1046, 10), 0); CHECK_EQ(wr1(0x1047, 60001), 3);
    /* parameters: window read */
    CHECK_EQ(rd(3, 0x1100, 0x2C), 0);
    CHECK_EQ(reg32(0x1104), 100000); CHECK_EQ(reg32(0x1106), 0);
    CHECK_EQ(reg(0x1108), 100); CHECK_EQ(reg(0x1129), 0); CHECK_EQ(reg(0x1128), 1);
    CHECK_EQ(reg32(0x112A), 2000000);
    CHECK_EQ(reg(0x1103), 10);
    CHECK_EQ(wr1(0x1103, 0), 3); CHECK_EQ(wr1(0x1103, 1001), 3); CHECK_EQ(wr1(0x1103, 1000), 0);
    CHECK_EQ(wr1(0x1100, 1), 3);                                        /* CW/CCW unsupported */
    CHECK_EQ(wr1(0x110A, 1), 3);                                        /* S curve unsupported */
    CHECK_EQ(wr1(0x1129, 1), 0); CHECK_EQ(wr1(0x1129, 2), 3);          /* svon_mode */
    CHECK_EQ(wr32(0x1104, 1000000), 0);
    CHECK_EQ(wr32(0x1044, 600000), 0);                                  /* now below max_speed */
    CHECK_EQ(wr32(0x1104, 500000), 0);                                  /* max_speed below the stored move speed: not checked here, only at start */
    CHECK_EQ(wr32(0x1106, 600000), 3);                                  /* start_speed above max_speed */
    CHECK_EQ(wr32(0x112A, 3000000), 3); CHECK_EQ(wr32(0x112A, 10000000), 0);
    CHECK_EQ(wr1(0x1128, 0), 0);
    CHECK_EQ(rd(3, 0x1100 + 0x2C, 1), 0);                               /* reserved tail of the block reads 0 */
    CHECK_EQ(wr1(0x1100 + 0x2C, 0), 2);
}

static void test_one_axis_model(void)
{
    setup(1, false);
    CHECK_EQ(rd(3, 0x2000, 1), 2);                                      /* axis 2 area absent */
    CHECK_EQ(wr1(0x2040, 1), 2);
    CHECK_EQ(wr32(0x2042, 5), 2);
    CHECK_EQ(coil_r(0x20, 1), -2); CHECK_EQ(coil_w(0x23, true), 2);
    CHECK_EQ(reg(0x1000), 0);                                           /* axis 1 area identical to the 2-axis model */
    CHECK_EQ(wr32(0x1042, 5), 0);
    CHECK_EQ(reg(0x21), 1);
}

/* [T-reg-atomic] a rejected multi-register write changes nothing and runs no command */
static void test_atomic_write(void)
{
    setup(2, false);
    uint16_t v[2] = {1, 0};                                             /* dir_invert=1 valid, dir_setup_us=0 invalid */
    CHECK_EQ(wr(0x1102, v, 2), 3);
    CHECK_EQ(reg(0x1102), 0);                                           /* dir_invert unchanged */
    CHECK_EQ(R.cfg_changed_n, 0);
    uint16_t w[3] = {0xFFFF, 0xFFFF, 99};                               /* target + only the low half of move_speed */
    CHECK_EQ(wr(0x1042, w, 3), 3);                                      /* 32-bit item written by halves: exception 03 */
    CHECK_EQ(reg32(0x1042), 0);
    CHECK_EQ(R.axis_cmd_n, 0);
    uint16_t ok[4] = {(uint16_t)-1, 0xFFFF, 10000 & 0xFFFF, 0};         /* target and speed in one FC16 */
    CHECK_EQ(wr(0x1042, ok, 4), 0);
    CHECK_EQ((int32_t)reg32(0x1042), -1);
    uint16_t two[2] = {FW_CMD_START_REL, 0};                            /* 0x1040 command + 0x1041 (reserved, undefined) */
    CHECK_EQ(wr(0x1040, two, 2), 2);
    CHECK_EQ(R.axis_cmd_n, 0);                                          /* no command ran although the first word was valid */
}

static void test_commands(void)
{
    setup(2, false);
    CHECK_EQ(wr1(0x1040, FW_CMD_START_REL), 0); CHECK_EQ(R.axis_cmd_n, 1); CHECK_EQ(R.last_axis, 0); CHECK_EQ(R.last_cmd, FW_CMD_START_REL);
    CHECK_EQ(wr1(0x2040, FW_CMD_STOP), 0); CHECK_EQ(R.last_axis, 1); CHECK_EQ(R.last_cmd, FW_CMD_STOP);
    CHECK_EQ(wr1(0x1040, 0), 0); CHECK_EQ(R.axis_cmd_n, 2);             /* 0 = no command */
    for (uint16_t c = 1; c <= 12; ++c) {
        const bool unsupported = c == FW_CMD_HOME || c == FW_CMD_JOG_POS || c == FW_CMD_JOG_NEG || c == FW_CMD_JOG_STOP;
        const int n0 = R.axis_cmd_n;
        D.ax[0].cmd_result = 0;
        CHECK_EQ(wr1(0x1040, c), unsupported ? 3 : 0);
        CHECK_EQ(R.axis_cmd_n, n0 + (unsupported ? 0 : 1));
        if (unsupported) CHECK_EQ(D.ax[0].cmd_result, FW_RES_RANGE);    /* HOME / JOG are not part of v1 */
    }
    CHECK_EQ(wr1(0x1040, 13), 3);
    /* axis coils: 0x10 * n */
    CHECK_EQ(coil_w(0x12, true), 0); CHECK_EQ(R.last_axis, 0); CHECK_EQ(R.last_cmd, FW_CMD_STOP);
    CHECK_EQ(coil_w(0x22, true), 0); CHECK_EQ(R.last_axis, 1);
    CHECK_EQ(coil_w(0x23, true), 0); CHECK_EQ(R.last_cmd, FW_CMD_SERVO_ON);
    CHECK_EQ(coil_w(0x10, true), 2); CHECK_EQ(coil_w(0x21, true), 2);   /* JOG coils unsupported */
    CHECK_EQ(coil_w(0x10, false), 0);                                   /* releasing a JOG button is harmless */
    D.ax[1].servo_manual = true; CHECK_EQ(coil_r(0x23, 1), 1);
}

static void test_config_change_notification(void)
{
    setup(2, false);
    CHECK_EQ(wr1(0x1103, 20), 0); CHECK_EQ(R.cfg_changed_n, 1);
    CHECK_EQ(wr1(0x1103, 20), 0); CHECK_EQ(R.cfg_changed_n, 1);          /* same value: no new save request */
    CHECK_EQ(wr1(0x1103, 0), 3); CHECK_EQ(R.cfg_changed_n, 1);          /* rejected: none */
    CHECK_EQ(wr1(0x1040, FW_CMD_STOP), 0); CHECK_EQ(R.cfg_changed_n, 1); /* commands are not configuration */
    CHECK_EQ(wr1(0x120, 1), 0); CHECK_EQ(R.cfg_changed_n, 1);           /* nor are the user outputs */
}

/* ---------------------------------------------------------------- network / mode / history */
static void test_network_and_mode(void)
{
    setup(2, false);                                                    /* TCP model */
    CHECK_EQ(reg32(0x4000), 0xC0A800DCu); CHECK_EQ(reg32(0x4002), 0xFFFFFF00u); CHECK_EQ(reg32(0x4004), 0);
    CHECK_EQ(reg(0x4006), 1); CHECK_EQ(reg(0x400A), 96); CHECK_EQ(reg(0x400B), 0); CHECK_EQ(reg(0x400C), 1);
    CHECK_EQ(reg(0x400E), FW_MODE_TCP);
    CHECK_EQ(wr1(0x400E, FW_MODE_RTU), 3);                              /* TCP model: RTU refused */
    CHECK_EQ(reg(0x400E), FW_MODE_TCP);
    CHECK_EQ(wr1(0x400E, FW_MODE_TCP), 0);
    D.mac[0] = 0x02; D.mac[1] = 0x11; D.mac[5] = 0xFE;
    CHECK_EQ(reg(0x4007), 0x0211); CHECK_EQ(reg(0x4009), 0x00FE);
    CHECK_EQ(wr1(0x4007, 0), 2);                                        /* MAC read-only */
    CHECK_EQ(wr32(0x4000, 0), 3);                                       /* ip 0.0.0.0 */
    CHECK_EQ(wr32(0x4002, 0xFF00FF00u), 3);
    CHECK_EQ(wr1(0x400A, 144), 3);
    CHECK_EQ(wr32(0x4000, 0xC0A80164u), 0); CHECK_EQ(reg32(0x4000), 0xC0A80164u);
    CHECK_EQ(wr1(0x4006, 248), 3);
    CHECK_EQ(wr1(0x400F, 2), 3); CHECK_EQ(wr1(0x400F, 1), 0); CHECK_EQ(R.last_sys, FW_SYS_NET_APPLY);
    setup(2, true);                                                     /* RTU model */
    CHECK_EQ(wr1(0x400E, FW_MODE_RTU), 0); CHECK_EQ(reg(0x400E), FW_MODE_RTU);
    CHECK_EQ(wr1(0x400E, 2), 3);
    CHECK_EQ(wr1(0x400A, 1152), 0); CHECK_EQ(wr1(0x400B, 1), 0); CHECK_EQ(wr1(0x400C, 247), 0); CHECK_EQ(wr1(0x400C, 248), 3);
}

static void test_history(void)
{
    setup(2, false);
    CHECK_EQ(reg(0x3000), 0);
    D.uptime_s = 10; fw_hist_push(&D, FW_AL_WDT, 0);
    D.uptime_s = 20; fw_hist_push(&D, FW_AL_ESTOP, 1);
    CHECK_EQ(reg(0x3000), 2);
    CHECK_EQ(reg(0x3010), FW_AL_ESTOP); CHECK_EQ(reg(0x3011), 1); CHECK_EQ(reg32(0x3012), 20);   /* newest first */
    CHECK_EQ(reg(0x3014), FW_AL_WDT); CHECK_EQ(reg32(0x3016), 10);
    CHECK_EQ(reg(0x3018), 0);
    for (int i = 0; i < 40; ++i) fw_hist_push(&D, 0x0401, 2);
    CHECK_EQ(reg(0x3000), FW_HIST_N);
    CHECK_EQ(rd(3, 0x3010, 125), 0);                                    /* 32 x 4 = 128 words needs two reads */
    CHECK_EQ(rd(3, 0x3010 + 125, 3), 0);
    CHECK_EQ(wr1(0x3000, 0), 2);
}

/* ---------------------------------------------------------------- framing */
static void test_framing_and_exceptions(void)
{
    setup(2, false);
    uint8_t q[8] = {43, 0, 0, 0, 0};
    call(q, 5); CHECK_EQ(RESP[0], 43 | 0x80); CHECK_EQ(RESP[1], 1);     /* unsupported function */
    uint8_t s[5] = {3, 0, 0, 0, 0};
    call(s, 5); CHECK_EQ(RESP[1], 3);                                   /* count 0 */
    uint8_t b[5] = {3, 0, 0, 0, 126};
    call(b, 5); CHECK_EQ(RESP[1], 3);                                   /* more than 125 */
    uint8_t o[5] = {3, 0, 0, 0, 125};
    call(o, 5); CHECK_EQ(RESP[0], 3);
    uint8_t sh[4] = {3, 0, 0, 0};
    call(sh, 4); CHECK_EQ(RESP[1], 3);                                  /* short PDU */
    uint8_t bad16[8] = {16, 0x10, 0x42, 0, 2, 3, 0, 0};                 /* byte count != 2n */
    call(bad16, 8); CHECK_EQ(RESP[1], 3);
    uint8_t c5[5] = {5, 0, 0, 0x12, 0x34};                              /* neither 0x0000 nor 0xFF00 */
    call(c5, 5); CHECK_EQ(RESP[1], 3);
    CHECK_EQ(fw_mb_process(&D, q, 0, RESP), 0);
    CHECK(fw_mb_is_command(s, 1) == false);
    uint8_t w6[1] = {6}; CHECK(fw_mb_is_command(w6, 1));
    uint8_t r16[1] = {16}; CHECK(fw_mb_is_command(r16, 1));
    uint8_t r15[1] = {15}; CHECK(fw_mb_is_command(r15, 1));
}

int main(void)
{
    RUN(test_legacy_read); RUN(test_legacy_write); RUN(test_legacy_coils); RUN(test_system_area);
    RUN(test_system_cfg_and_commands); RUN(test_axis_area_layout); RUN(test_axis_op_and_params); RUN(test_one_axis_model);
    RUN(test_atomic_write); RUN(test_commands); RUN(test_config_change_notification); RUN(test_network_and_mode);
    RUN(test_history); RUN(test_framing_and_exceptions);
    SUMMARY();
}
