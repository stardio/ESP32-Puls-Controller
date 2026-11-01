#include "fw_axis.h"
#include <string.h>

#define PAR(a) ((a)->dev->cfg.par[(a)->idx])
#define OP(a)  ((a)->dev->cfg.op[(a)->idx])
#define TELE(a) ((a)->dev->ax[(a)->idx])

static uint16_t init_alarm(const fw_axis_t *a)
{
    const uint16_t b = a->dev->boot_status;
    const uint16_t own = a->idx == 0 ? FW_BOOT_PULSE_X : FW_BOOT_PULSE_Y;
    const uint16_t fault = a->idx == 0 ? FW_BOOT_FAULT_X : FW_BOOT_FAULT_Y;
    if (!(b & own) || !fw_hal_pulse_ready(a->idx)) return FW_AL_PE_HW;
    if (!(b & fault)) return FW_AL_PE_FAULTPATH;                      /* M-06: MCPWM fault path (D-070 path B) not bound: no operation */
    if (!(b & FW_BOOT_CFG_OK)) return FW_AL_PARAM;
    if (!(b & FW_BOOT_NVS)) return FW_AL_NVM;
    if (!(b & FW_BOOT_WDT)) return FW_AL_PE_HW;
    return FW_AL_NONE;
}

static bool engine_allowed(const fw_axis_t *a)
{
    const uint16_t own = a->idx == 0 ? FW_BOOT_PULSE_X : FW_BOOT_PULSE_Y;
    const uint16_t fault = a->idx == 0 ? FW_BOOT_FAULT_X : FW_BOOT_FAULT_Y;
    const uint16_t need = (uint16_t)(FW_BOOT_OPERATE | own | fault);
    return (a->dev->boot_status & need) == need && fw_hal_pulse_ready(a->idx);
}

bool fw_axis_running(const fw_axis_t *a) { return a->mv != MV_IDLE; }
uint16_t fw_axis_sticky_alarm(const fw_axis_t *a) { return a->sticky_alarm; }

void fw_axis_init(fw_axis_t *a, fw_dev_t *dev, uint8_t idx, uint32_t res_hz)
{
    memset(a, 0, sizeof(*a));
    a->dev = dev; a->idx = idx; a->res_hz = res_hz;
    a->pos_valid = false;                         /* no feedback: the position is unknown after power-up */
}

static void set_servo(fw_axis_t *a)
{
    const bool auto_on = PAR(a)[FW_P_SVON_MODE] == 0 && a->mv != MV_IDLE;
    const bool want = TELE(a).servo_manual || auto_on;
    if (want != a->servo_out) { a->servo_out = want; fw_hal_servo_set(a->idx, want); }
}

void fw_axis_servo_boot(fw_axis_t *a)
{
    if (PAR(a)[FW_P_SVON_AT_BOOT]) TELE(a).servo_manual = true;
    set_servo(a);
}

static uint32_t cur_signed_pos(const fw_axis_t *a, uint32_t n)
{
    return a->forward_positive ? (uint32_t)a->position + n : (uint32_t)a->position - n;
}

/* common end-of-move bookkeeping. cause = stop cause code, result = legacy result (1 ok, 2 stop, 3 mismatch) */
static void finalize(fw_axis_t *a, uint16_t cause, uint16_t leg_result, uint16_t leg_fault)
{
    fw_axis_tele_t *t = &TELE(a);
    fw_hal_pulse_stop(a->idx);
    const uint32_t n = fw_hal_pulse_count(a->idx);
    fw_hal_pulse_end(a->idx);
    a->position = (int32_t)cur_signed_pos(a, n);
    a->mv = MV_IDLE;
    a->test_active = false;
    a->last_hz = 0;
    t->pulses_done = n;
    if (n > 0) t->loop_seen = (leg_result != 3);              /* PCNT counted the output pin; 3 = count mismatch */
    t->leg_result = leg_result;
    t->leg_fault = leg_fault;
    t->last_stop_cause = cause;
    t->last_ok = (leg_result == 1);
    a->done_ok = (cause == FW_SC_DONE && leg_result == 1);
    if (cause == FW_SC_DONE && leg_result == 1) t->move_count++;
    t->cmd_speed = 0;
    set_servo(a);
}

static uint8_t reject(fw_axis_t *a, uint8_t code, bool legacy_fault)
{
    TELE(a).cmd_result = code;
    if (legacy_fault && a->idx == 0) { TELE(a).leg_result = 0; TELE(a).leg_fault = 1; }
    return code;
}

/* start a finite move. delta_forward: direction, pulses >= 1 */
static uint8_t start_move(fw_axis_t *a, bool forward, uint32_t pulses, uint32_t speed, uint16_t accel, uint16_t decel)
{
    fw_axis_tele_t *t = &TELE(a);
    const uint16_t *par = PAR(a);
    if (!engine_allowed(a)) return reject(a, FW_RES_ALARM, true);
    /* STOP input active, or io not initialised / no STOP input: refuse at once (no side effect). A merely LATCHED STOP
     * with the input released is handled after all other checks (D-072, below). */
    if (fw_hal_stop_input(a->idx) || (fw_hal_stop_blocking(a->idx) && !fw_hal_stop_latched(a->idx)))
        return reject(a, FW_RES_ESTOP, true);
    if (a->sticky_alarm != FW_AL_NONE) return reject(a, FW_RES_ALARM, true);
    if (a->mv != MV_IDLE) return reject(a, FW_RES_RUNNING, false);
    if (par[FW_P_SVON_MODE] == 1 && !t->servo_manual) return reject(a, FW_RES_SERVO_OFF, true);
    if (pulses == 0) return reject(a, FW_RES_RANGE, true);

    const uint32_t start = fw_start_speed(fw_w32(par, FW_P_START_SPEED), speed);
    uint32_t ticks;
    if (!fw_ticks_for_freq(a->res_hz, start, FW_MAX_PERIOD_TICKS, &ticks) ||
        !fw_ticks_for_freq(a->res_hz, speed, FW_MAX_PERIOD_TICKS, &ticks))
        return reject(a, FW_RES_RANGE, true);

    /* D-072: every other check passed, so this is a valid explicit start. A latched STOP whose input has been inactive
     * long enough is released by it; otherwise 0x10 and the latch stays. A rejected command never releases the latch. */
    if (fw_hal_stop_latched(a->idx) && fw_hal_stop_release(a->idx) != 0) return reject(a, FW_RES_ESTOP, true);

    fw_profile_calc(&a->prof, pulses, start, speed, accel, decel);
    a->forward_positive = forward;
    a->target_pulses = pulses;
    a->user_cause = 0;
    a->mv = MV_RUN;                                 /* servo output follows */
    set_servo(a);
    fw_hal_dir_set(a->idx, (forward != (par[FW_P_DIR_INVERT] != 0)));
    fw_hal_delay_us(par[FW_P_DIR_SETUP_US]);
    const int e = fw_hal_pulse_start(a->idx, start, pulses);
    if (e != FW_PE_OK) {
        a->mv = MV_IDLE; set_servo(a);
        return reject(a, e == FW_PE_BAD_ARG ? FW_RES_RANGE : e == FW_PE_STOP_BLOCK ? FW_RES_ESTOP :
                         e == FW_PE_BUSY ? FW_RES_RUNNING : FW_RES_ALARM, true);
    }
    a->last_hz = start;
    t->cmd_result = FW_RES_OK;
    t->leg_result = 0; t->leg_fault = 0; t->pulses_done = 0;
    t->last_ok = false; a->done_ok = false;
    t->target_position_act = (int32_t)(forward ? (uint32_t)a->position + pulses : (uint32_t)a->position - pulses);
    return FW_RES_OK;
}

static uint8_t cmd_start_with(fw_axis_t *a, bool absolute, int32_t target, uint32_t speed, uint16_t accel, uint16_t decel)
{
    fw_axis_tele_t *t = &TELE(a);
    if (absolute) {
        if (!a->pos_valid && PAR(a)[FW_P_REQUIRE_HOME]) return reject(a, FW_RES_POS_INVALID, false);
        const int64_t delta = (int64_t)target - (int64_t)a->position;
        if (delta == 0) {                           /* already there: complete at once */
            if (a->mv != MV_IDLE) return reject(a, FW_RES_RUNNING, false);
            t->cmd_result = FW_RES_OK; t->leg_result = 1; t->leg_fault = 0; t->pulses_done = 0;
            t->last_ok = true; a->done_ok = true; t->last_stop_cause = FW_SC_DONE;
            return FW_RES_OK;
        }
        const uint64_t mag = delta < 0 ? (uint64_t)(-delta) : (uint64_t)delta;
        return start_move(a, delta > 0, (uint32_t)mag, speed, accel, decel);
    }
    if (target == 0) return reject(a, FW_RES_RANGE, false);
    const uint32_t mag = target < 0 ? (uint32_t)(-(int64_t)target) : (uint32_t)target;
    return start_move(a, target > 0, mag, speed, accel, decel);
}

static uint8_t cmd_start(fw_axis_t *a, bool absolute)
{
    const uint16_t *op = OP(a);
    return cmd_start_with(a, absolute, (int32_t)fw_w32(op, FW_O_TARGET), fw_w32(op, FW_O_SPEED), op[FW_O_ACCEL], op[FW_O_DECEL]);
}

/* ---- 5.12 helpers: converted speeds follow the same limits as a stored speed (100 Hz .. max_speed, >= start_speed) ---- */
static bool speed_ok(const fw_axis_t *a, uint32_t hz)
{
    const uint32_t st = fw_w32(PAR(a), FW_P_START_SPEED);
    return hz >= FW_SPEED_MIN_HZ && hz <= fw_w32(PAR(a), FW_P_MAX_SPEED) && hz <= FW_SPEED_MAX_HZ && !(st && hz < st);
}

static uint8_t cmd_start_unit(fw_axis_t *a, bool absolute)
{
    const uint16_t *op = OP(a);
    int32_t pulses; uint32_t hz;
    if (PAR(a)[FW_P_UNIT_MODE] == FW_UM_PULSE) return reject(a, FW_RES_RANGE, false);
    if (!fw_unit_to_pulses(PAR(a), (int32_t)fw_w32(op, FW_O_TARGET_UNIT), &pulses) ||
        !fw_unit_speed_to_hz(PAR(a), fw_w32(op, FW_O_SPEED_UNIT), &hz) || !speed_ok(a, hz))
        return reject(a, FW_RES_RANGE, false);                 /* overflow or speed over 1 MHz / max_speed: 0x17 */
    return cmd_start_with(a, absolute, pulses, hz, op[FW_O_ACCEL], op[FW_O_DECEL]);
}

static uint8_t cmd_point_go(fw_axis_t *a)
{
    const uint16_t *pt = a->dev->cfg.pt[a->idx][OP(a)[FW_O_POINT_INDEX] & (FW_PT_N - 1)];
    int32_t pulses; uint32_t hz;
    if (pt[FW_PT_VALID] != 1) return reject(a, FW_RES_RANGE, false);
    if (!fw_unit_to_pulses(PAR(a), (int32_t)fw_w32(pt, FW_PT_POS), &pulses) ||
        !fw_unit_speed_to_hz(PAR(a), fw_w32(pt, FW_PT_SPEED), &hz) || !speed_ok(a, hz))
        return reject(a, FW_RES_RANGE, false);
    return cmd_start_with(a, true, pulses, hz, pt[FW_PT_ACCEL], pt[FW_PT_DECEL]);
}

static uint8_t cmd_point_teach(fw_axis_t *a)
{
    uint16_t *pt = a->dev->cfg.pt[a->idx][OP(a)[FW_O_POINT_INDEX] & (FW_PT_N - 1)];
    int32_t pos_u; uint32_t sp_u;
    if (a->mv != MV_IDLE) return reject(a, FW_RES_RUNNING, false);
    if (!a->pos_valid) return reject(a, FW_RES_POS_INVALID, false);
    if (!fw_pulses_to_unit(PAR(a), a->position, &pos_u) || !fw_hz_to_unit(PAR(a), fw_w32(OP(a), FW_O_SPEED), &sp_u) || sp_u < 1u)
        return reject(a, FW_RES_RANGE, false);
    fw_set32(pt, FW_PT_POS, (uint32_t)pos_u); fw_set32(pt, FW_PT_SPEED, sp_u);
    pt[FW_PT_ACCEL] = OP(a)[FW_O_ACCEL]; pt[FW_PT_DECEL] = OP(a)[FW_O_DECEL]; pt[FW_PT_VALID] = 1;
    if (a->dev->ops.cfg_changed) a->dev->ops.cfg_changed(a->dev->ops.ctx);
    TELE(a).cmd_result = FW_RES_OK;
    return FW_RES_OK;
}

/* TEST_POS / TEST_NEG (FR-032): a finite move of INT32_MAX pulses at test_rpm; the command must be rewritten within
 * FW_TEST_KEEPALIVE_MS (checked in fw_axis_poll), otherwise it decelerates to a stop. The same command again = keepalive. */
static uint8_t cmd_test(fw_axis_t *a, bool forward)
{
    uint32_t hz;
    if (fw_hal_stop_input(a->idx) || (fw_hal_stop_blocking(a->idx) && !fw_hal_stop_latched(a->idx)) ) return reject(a, FW_RES_ESTOP, false);
    if (a->test_active && a->mv == MV_RUN && a->test_forward == forward) {
        a->test_last_ms = fw_hal_ms();
        TELE(a).cmd_result = FW_RES_OK;
        return FW_RES_OK;
    }
    if (!fw_test_hz(PAR(a), &hz) || !speed_ok(a, hz)) return reject(a, FW_RES_RANGE, false);
    if (a->mv != MV_IDLE) return reject(a, FW_RES_RUNNING, false);
    const uint8_t r = start_move(a, forward, (uint32_t)INT32_MAX, hz, OP(a)[FW_O_ACCEL], OP(a)[FW_O_DECEL]);
    if (r == FW_RES_OK) { a->test_active = true; a->test_forward = forward; a->test_last_ms = fw_hal_ms(); }
    return r;
}

uint8_t fw_axis_start_legacy(fw_axis_t *a)
{
    const uint32_t pulses = fw_w32(a->dev->cfg.leg, 0);
    if (pulses > (uint32_t)INT32_MAX) return reject(a, FW_RES_RANGE, true);   /* > S32 limit of the axis area (spec 5.0.1) */
    return start_move(a, (a->dev->cfg.leg[2] & 1u) != 0, pulses, fw_w32(OP(a), FW_O_SPEED), OP(a)[FW_O_ACCEL], OP(a)[FW_O_DECEL]);
}

uint8_t fw_axis_start_physical(fw_axis_t *a)
{
    if (a->idx == 0) return fw_axis_start_legacy(a);
    return cmd_start(a, false);
}

void fw_axis_legacy_reset(fw_axis_t *a)
{
    fw_axis_tele_t *t = &TELE(a);
    t->pulses_done = 0; t->leg_result = 0; t->leg_fault = 0; t->last_ok = false;
}

static void begin_decel(fw_axis_t *a, uint16_t cause)
{
    if (a->mv != MV_RUN) return;
    uint32_t stop_at = 0;
    const uint32_t n = fw_hal_pulse_count(a->idx);
    a->user_cause = cause;
    if (fw_profile_plan_stop(&a->prof, n, a->last_hz, &stop_at)) {
        a->stop_at = stop_at;
        a->mv = MV_DECEL;
    }
    /* else: the move already ends sooner than a decelerated stop would; it completes normally */
}

void fw_axis_decel_all_reason(fw_axis_t *a, uint16_t alarm_code)
{
    if (alarm_code != FW_AL_NONE && a->sticky_alarm == FW_AL_NONE) {
        a->sticky_alarm = alarm_code; TELE(a).alarm_count++;
        fw_hist_push(a->dev, alarm_code, (uint16_t)(a->idx + 1));
    }
    begin_decel(a, FW_SC_STOP_CMD);
}

uint8_t fw_axis_command_gated(fw_axis_t *a, uint16_t cmd, bool start_blocked)
{
    const bool keepalive = (cmd == FW_CMD_TEST_POS || cmd == FW_CMD_TEST_NEG) && fw_axis_running(a);
    if (start_blocked && fw_cmd_is_start(cmd) && !keepalive) {
        a->dev->ax[a->idx].cmd_result = FW_RES_RUNNING;
        return FW_RES_RUNNING;
    }
    return fw_axis_command(a, cmd);
}

uint8_t fw_axis_command(fw_axis_t *a, uint16_t cmd)
{
    fw_axis_tele_t *t = &TELE(a);
    switch (cmd) {
    case FW_CMD_START_ABS: return cmd_start(a, true);
    case FW_CMD_START_REL: return cmd_start(a, false);
    case FW_CMD_START_ABS_UNIT: return cmd_start_unit(a, true);
    case FW_CMD_START_REL_UNIT: return cmd_start_unit(a, false);
    case FW_CMD_TEST_POS: return cmd_test(a, true);
    case FW_CMD_TEST_NEG: return cmd_test(a, false);
    case FW_CMD_POINT_GO: return cmd_point_go(a);
    case FW_CMD_POINT_TEACH: return cmd_point_teach(a);
    case FW_CMD_STOP:
        begin_decel(a, FW_SC_STOP_CMD);
        t->cmd_result = FW_RES_OK;
        return FW_RES_OK;
    case FW_CMD_QUICK_STOP:
        if (a->mv != MV_IDLE) finalize(a, FW_SC_QUICK, 2, 0);
        t->cmd_result = FW_RES_OK;
        return FW_RES_OK;
    case FW_CMD_ALARM_RESET:
        if (fw_hal_stop_blocking(a->idx)) return reject(a, FW_RES_ESTOP, false);   /* D-058: no reset for the STOP latch */
        if (a->mv != MV_IDLE) return reject(a, FW_RES_RUNNING, false);
        a->sticky_alarm = a->init_alarm;                           /* init faults stay until their cause is gone */
        if (a->dev->sys_status & FW_SSB_NVM_ERR) a->dev->sys_status = (uint16_t)(a->dev->sys_status & ~FW_SSB_NVM_ERR);
        t->cmd_result = FW_RES_OK;
        return FW_RES_OK;
    case FW_CMD_SERVO_ON:
        t->servo_manual = true; set_servo(a); t->cmd_result = FW_RES_OK; return FW_RES_OK;
    case FW_CMD_SERVO_OFF:
        if (a->mv != MV_IDLE) return reject(a, FW_RES_RUNNING, false);
        t->servo_manual = false; set_servo(a); t->cmd_result = FW_RES_OK; return FW_RES_OK;
    case FW_CMD_SET_POSITION:
        if (a->mv != MV_IDLE) return reject(a, FW_RES_RUNNING, false);
        a->position = (int32_t)fw_w32(OP(a), FW_O_TARGET);
        a->pos_valid = true;
        t->cmd_result = FW_RES_OK;
        return FW_RES_OK;
    default:
        return reject(a, FW_RES_RANGE, false);      /* HOME, JOG*: not in v1 */
    }
}

void fw_axis_poll(fw_axis_t *a)
{
    fw_axis_tele_t *t = &TELE(a);
    const bool latched = fw_hal_stop_latched(a->idx) || fw_hal_stop_input(a->idx);   /* M-02: level too, not only the edge latch */
    const bool blocking = fw_hal_stop_blocking(a->idx);

    if (a->mv != MV_IDLE) {
        if (latched) {
            /* the ISR already cut the output; book the pulses that really went out */
            finalize(a, FW_SC_QUICK, 2, 1);
            if (PAR(a)[FW_P_ESTOP_POS] == 0) a->pos_valid = false;
        } else if (fw_hal_pulse_finished(a->idx)) {
            const uint32_t n = fw_hal_pulse_count(a->idx);
            finalize(a, FW_SC_DONE, n == a->target_pulses ? 1 : 3, n == a->target_pulses ? 0 : 2);
        } else {
            const uint32_t n = fw_hal_pulse_count(a->idx);
            if (a->mv == MV_DECEL && n >= a->stop_at) {
                finalize(a, a->user_cause ? a->user_cause : FW_SC_STOP_CMD, 2, 0);
            } else {
                const uint32_t f = fw_profile_freq(&a->prof, n);
                if (f != a->last_hz) {
                    if (fw_hal_pulse_set_freq(a->idx, f) == FW_PE_OK) a->last_hz = f;
                    else finalize(a, FW_SC_QUICK, 3, 3);              /* profile update failed (legacy fault 3) */
                }
                t->cmd_speed = a->forward_positive ? (int32_t)a->last_hz : -(int32_t)a->last_hz;
                t->pulses_done = n;
                if (n > 0) t->loop_seen = true;                      /* wiring_status: loopback counter is rising */
            }
        }
    }
    if (a->test_active && a->mv == MV_RUN && (uint32_t)(fw_hal_ms() - a->test_last_ms) > FW_TEST_KEEPALIVE_MS)
        begin_decel(a, FW_SC_STOP_CMD);                            /* keepalive missed: decelerate to a stop */
    if (blocking && !a->prev_blocking && PAR(a)[FW_P_ESTOP_POS] == 0) a->pos_valid = false;
    a->prev_blocking = blocking;

    const uint16_t ia = init_alarm(a);
    if (ia != a->init_alarm) {
        if (ia != FW_AL_NONE) {
            a->sticky_alarm = ia; t->alarm_count++;
            fw_hist_push(a->dev, ia, (uint16_t)(a->idx + 1));
        } else if (a->sticky_alarm == a->init_alarm) {
            a->sticky_alarm = FW_AL_NONE;
        }
        a->init_alarm = ia;
    }

    /* telemetry */
    const bool running = a->mv != MV_IDLE;
    const uint32_t n = running ? fw_hal_pulse_count(a->idx) : 0;
    t->cmd_position = (int32_t)(running ? cur_signed_pos(a, n) : (uint32_t)a->position);
    t->moving = running;
    t->alarm_code = blocking ? FW_AL_ESTOP : a->sticky_alarm;      /* the STOP latch itself is the memory of 0x0101 */
    uint16_t st = 0;
    const bool engine = engine_allowed(a);
    if (engine && !blocking && t->alarm_code == FW_AL_NONE && !running) st |= FW_ASB_READY;
    if (running) st |= FW_ASB_BUSY;
    if (a->done_ok) st |= FW_ASB_DONE;
    if (t->alarm_code != FW_AL_NONE) st |= FW_ASB_ALARM;
    if (blocking) st |= FW_ASB_ESTOP;
    if (a->servo_out) st |= FW_ASB_SERVO;
    if (!a->pos_valid) st |= FW_ASB_POS_INVALID;
    t->status = st;
    t->state = blocking ? FW_AS_ESTOP : a->mv == MV_DECEL ? FW_AS_DECEL_STOP : t->alarm_code ? FW_AS_ALARM :
               running ? FW_AS_POSITIONING : (PAR(a)[FW_P_SVON_MODE] == 1 && !a->servo_out) ? FW_AS_SERVO_OFF : FW_AS_IDLE;
    set_servo(a);
}
