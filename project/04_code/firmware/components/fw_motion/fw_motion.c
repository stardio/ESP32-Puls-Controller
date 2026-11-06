#include "fw_motion.h"
#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "fw_axis.h"
#include "fw_cfg.h"
#include "fw_hal.h"
#include "fw_interp.h"

static const char *TAG = "FW_MOTION";
#define PERIOD_MS CONFIG_FW_UPDATE_MS

static fw_dev_t s_dev;
static fw_axis_t s_ax[FW_MAX_AXES];
static SemaphoreHandle_t s_lock;
static fw_save_t s_save;
static fw_commloss_t s_cl;
static volatile uint8_t s_start_pending;
static volatile uint8_t s_sync_pending;          /* D-088: b0 simultaneous START, b1 simultaneous STOP */
static bool s_ip_start_req;                      /* D-089/I-049: INTERP_START accepted, planned by motion_task (under the lock) */
static bool s_saving, s_reboot_req, s_reboot_commit, s_cl_tripped;   /* s_saving / s_reboot_commit: set under the lock, no START */
static uint32_t s_reboot_at_ms;
static uint32_t s_hb_ms;
static fw_nrhold_t s_nrhold;                     /* D-076: X START + STOP held 10 s from boot */

void fw_motion_lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
void fw_motion_unlock(void) { xSemaphoreGive(s_lock); }
const fw_dev_t *fw_motion_dev(void) { return &s_dev; }
uint16_t fw_motion_comm_mode(void) { return s_dev.comm_mode_active; }
void fw_motion_net_words(uint16_t net[FW_NET_WORDS])
{
    fw_motion_lock(); memcpy(net, s_dev.cfg.net, sizeof(s_dev.cfg.net)); fw_motion_unlock();
}
void fw_motion_set_link(bool up) { fw_motion_lock(); s_dev.link = up; fw_motion_unlock(); }
void fw_motion_set_clients(unsigned n) { fw_motion_lock(); s_dev.tcp_clients = (uint16_t)n; fw_motion_unlock(); }
void fw_motion_note_rtu_rx(void) { fw_motion_lock(); s_dev.now_ms = fw_hal_ms(); fw_dev_note_rtu_rx(&s_dev); fw_motion_unlock(); }
void fw_motion_set_mac(const uint8_t mac[6]) { fw_motion_lock(); memcpy(s_dev.mac, mac, 6); fw_motion_unlock(); }

/* ------------------------------------------------------------------ D-089 X/Y interpolation */
#define IP_K         200.0       /* 1/s: position error gain (5 ms). IP_K * PERIOD_MS < 1000 so the end is never overshot */
#define IP_F_MIN     50.0        /* Hz: slower axis demand parks the axis (MCPWM floor at 2 MHz resolution = 31 Hz) */
#define IP_SETTLE_MS 3000u       /* after the planned end: time allowed to reach the exact end point */
#define IP_DIR_SETUP_MAX_US 200u /* [recommended] restarts busy-wait dir_setup_us (twice on a reversal) inside the lock */
static struct {
    bool     active;
    bool     commloss;           /* review M-02: comm loss seen; the alarm is raised after the path deceleration */
    fw_ip_t  p;
    int32_t  base[2], fin[2];    /* start position, end position (absolute pulses) */
    uint32_t t0_ms;
} s_ip;

/* review M-03: from the moment motion_task decides to save / reboot (under the lock) no start is accepted.
 * D-089: nor while an interpolation owns both axes (an axis parked mid-path looks idle). */
static bool start_blocked(void) { return s_saving || s_reboot_commit || s_ip.active; }

static bool any_running(void)
{
    if (s_ip.active) return true;
    for (unsigned a = 0; a < s_dev.axis_count; ++a) if (fw_axis_running(&s_ax[a])) return true;
    return false;
}

static double axis_max_hz(unsigned a)
{
    const uint32_t m = fw_w32(s_dev.cfg.par[a], FW_P_MAX_SPEED);
    return m < FW_SPEED_MAX_HZ ? (double)m : (double)FW_SPEED_MAX_HZ;
}

static void ip_finish(uint16_t state, uint16_t result, uint16_t cause)
{
    for (unsigned a = 0; a < 2; ++a) fw_axis_stream_session(&s_ax[a], false, cause, 0);
    s_ip.active = false;
    s_dev.ip_state = state; s_dev.ip_result = result;
    if (s_ip.commloss)                                  /* M-02: now the comm-loss alarm (axes already idle) */
        for (unsigned a = 0; a < 2; ++a) fw_axis_decel_all_reason(&s_ax[a], FW_AL_COMMLOSS);
    s_ip.commloss = false;
}

static uint16_t ip_refuse(uint16_t result)
{
    s_dev.ip_result = result;
    return FW_SRES_REFUSED;
}

/* sys_command INTERP_START: plan from the 0x0150 block and the current positions; both axes or nothing */
static uint16_t ip_start(void)
{
    if (s_dev.axis_count < 2) return ip_refuse(FW_IP_E_RANGE);
    if (start_blocked()) { s_dev.ip_result = FW_RES_RUNNING; return FW_SRES_AXIS_RUNNING; }
    const uint16_t *ip = s_dev.ip;
    const bool absolute = ip[FW_IPO_ABS] != 0;
    for (unsigned a = 0; a < 2; ++a) {
        uint8_t r = fw_axis_ready_check(&s_ax[a]);
        if (r == FW_RES_OK && fw_hal_stop_latched(a)) r = FW_RES_ESTOP;     /* M-01: release the latch with a single-axis start */
        if (r == FW_RES_OK && absolute && !s_ax[a].pos_valid && s_dev.cfg.par[a][FW_P_REQUIRE_HOME]) r = FW_RES_POS_INVALID;
        if (r == FW_RES_OK && s_dev.cfg.par[a][FW_P_DIR_SETUP_US] > IP_DIR_SETUP_MAX_US) r = FW_RES_RANGE;   /* M-05 */
        if (r != FW_RES_OK) { s_dev.ax[a].cmd_result = r; return ip_refuse(r); }
    }
    const int32_t base[2] = {s_ax[0].position, s_ax[1].position};
    const int64_t ex = (int32_t)fw_w32(ip, FW_IPO_END_X), ey = (int32_t)fw_w32(ip, FW_IPO_END_Y);
    const int64_t dx = absolute ? ex - base[0] : ex, dy = absolute ? ey - base[1] : ey;
    if (dx < INT32_MIN || dx > INT32_MAX || dy < INT32_MIN || dy > INT32_MAX ||
        (int64_t)base[0] + dx > INT32_MAX || (int64_t)base[0] + dx < INT32_MIN ||
        (int64_t)base[1] + dy > INT32_MAX || (int64_t)base[1] + dy < INT32_MIN) return ip_refuse(FW_IP_E_RANGE);
    const int r = fw_ip_plan(&s_ip.p, (uint8_t)ip[FW_IPO_MODE], (int32_t)dx, (int32_t)dy, (int32_t)fw_w32(ip, FW_IPO_CTR_I),
                             (int32_t)fw_w32(ip, FW_IPO_CTR_J), fw_w32(ip, FW_IPO_FEED), ip[FW_IPO_ACCEL], ip[FW_IPO_DECEL]);
    if (r != FW_IP_OK) return ip_refuse((uint16_t)r);
    double vx, vy;
    fw_ip_axis_peak(&s_ip.p, &vx, &vy);
    if (vx > axis_max_hz(0) || vy > axis_max_hz(1)) return ip_refuse(FW_IP_E_RANGE);   /* feed too fast for an axis */
    s_ip.base[0] = base[0]; s_ip.base[1] = base[1];
    s_ip.fin[0] = (int32_t)(base[0] + dx); s_ip.fin[1] = (int32_t)(base[1] + dy);
    s_ip.t0_ms = s_dev.now_ms;                       /* the pass time ip_tick uses as "now" (a later read could be ahead) */
    s_ip.active = true;
    s_ip.commloss = false;
    for (unsigned a = 0; a < 2; ++a) fw_axis_stream_session(&s_ax[a], true, 0, s_ip.fin[a]);
    s_dev.ip_state = FW_IPS_RUN; s_dev.ip_result = FW_IP_OK;
    return FW_SRES_OK;
}

/* decelerate along the path (STOP, STOP_ALL, comm loss) or stop at once (QUICK_STOP) */
static void ip_stop(bool quick)
{
    if (!s_ip.active) return;
    if (quick) { ip_finish(FW_IPS_STOPPED, FW_IP_OK, FW_SC_QUICK); return; }
    const int32_t el = (int32_t)(fw_hal_ms() - s_ip.t0_ms);
    fw_ip_stop(&s_ip.p, (el > 0 ? el : 0) / 1000.0);
    double x, y;
    fw_ip_xy(&s_ip.p, fw_ip_end_s(&s_ip.p), &x, &y);
    s_ip.fin[0] = s_ip.base[0] + (int32_t)lround(x);
    s_ip.fin[1] = s_ip.base[1] + (int32_t)lround(y);
    s_dev.ip_state = FW_IPS_STOPPING;
}

/* every motion-task pass: path point -> axis frequency/direction (feed-forward + position error), exact end point */
static void ip_tick(uint32_t now)
{
    if (!s_ip.active) return;
    for (unsigned a = 0; a < 2; ++a) {               /* STOP input or alarm: abort (comm loss: decel along the path) */
        const uint16_t al = fw_axis_sticky_alarm(&s_ax[a]);
        if (fw_hal_stop_blocking(a) || (al != FW_AL_NONE && al != FW_AL_COMMLOSS)) {
            ip_finish(FW_IPS_ABORTED, FW_IP_E_ABORT, FW_SC_QUICK);
            return;
        }
    }
    const int32_t el = (int32_t)(now - s_ip.t0_ms);  /* signed: never a huge unsigned value if t0 is ahead */
    const uint32_t elapsed = el > 0 ? (uint32_t)el : 0u;
    const double t = elapsed / 1000.0, dt = PERIOD_MS / 1000.0;
    const bool ended = t >= s_ip.p.T;
    double x = 0, y = 0, x2 = 0, y2 = 0;
    if (!ended) {
        fw_ip_xy(&s_ip.p, fw_ip_s(&s_ip.p, t, NULL), &x, &y);
        fw_ip_xy(&s_ip.p, fw_ip_s(&s_ip.p, t + dt, NULL), &x2, &y2);
    }
    const double tgt[2] = {x, y}, vff[2] = {(x2 - x) / dt, (y2 - y) / dt};
    bool all_there = true;
    for (unsigned a = 0; a < 2; ++a) {
        const int32_t cur = fw_axis_cur_pos(&s_ax[a]);
        double f;
        if (ended) {
            const int32_t e = s_ip.fin[a] - cur;
            if (e == 0) { if (!fw_axis_stream_park(&s_ax[a])) all_there = false; continue; }
            all_there = false;
            f = IP_K * e;
            if (fabs(f) < IP_F_MIN) f = e > 0 ? IP_F_MIN : -IP_F_MIN;
        } else {
            all_there = false;
            const double err = tgt[a] - (double)(cur - s_ip.base[a]);
            f = vff[a] + IP_K * err;
            if (fabs(f) < IP_F_MIN) {
                if (fabs(err) < 0.5) { fw_axis_stream_park(&s_ax[a]); continue; }
                f = err > 0 ? IP_F_MIN : -IP_F_MIN;
            }
        }
        const uint8_t r = fw_axis_stream_run(&s_ax[a], f > 0, (uint32_t)fmin(fabs(f), axis_max_hz(a)));
        if (r != FW_RES_OK) { ip_finish(FW_IPS_ABORTED, r, FW_SC_QUICK); return; }
    }
    if (ended && all_there)
        ip_finish(s_dev.ip_state == FW_IPS_STOPPING ? FW_IPS_STOPPED : FW_IPS_DONE, FW_IP_OK,
                  s_dev.ip_state == FW_IPS_STOPPING ? FW_SC_STOP_CMD : FW_SC_DONE);
    else if (ended && elapsed > (uint32_t)(s_ip.p.T * 1000.0) + IP_SETTLE_MS)
        ip_finish(FW_IPS_ABORTED, FW_IP_E_TIMEOUT, FW_SC_QUICK);
}

static int update_oe(void)
{
    bool ok = (s_dev.boot_status & FW_BOOT_OPERATE) == FW_BOOT_OPERATE;
    for (unsigned a = 0; a < s_dev.axis_count; ++a)
        ok = ok && (s_dev.boot_status & (a == 0 ? FW_BOOT_PULSE_X : FW_BOOT_PULSE_Y));
    if (ok) { fw_hal_oe_allow(); s_dev.boot_status |= FW_BOOT_OE_ALLOWED; }
    else { fw_hal_oe_forbid(); s_dev.boot_status &= (uint16_t)~FW_BOOT_OE_ALLOWED; }
    return ok ? 0 : -1;
}

/* ------------------------------------------------------------------ ops (called with the lock held) */
static uint8_t op_axis_cmd(void *ctx, uint8_t axis, uint16_t cmd)
{
    (void)ctx;
    if (axis >= s_dev.axis_count) return FW_RES_RANGE;
    if (s_ip.active && (cmd == FW_CMD_STOP || cmd == FW_CMD_QUICK_STOP)) {      /* D-089: stop the path, not one axis */
        ip_stop(cmd == FW_CMD_QUICK_STOP);
        s_dev.ax[axis].cmd_result = FW_RES_OK;
        return FW_RES_OK;
    }
    return fw_axis_command_gated(&s_ax[axis], cmd, start_blocked());   /* one gate for every start command (R3-M1) */
}
static uint8_t op_legacy_start(void *ctx)
{
    (void)ctx;
    if (start_blocked()) { s_dev.ax[0].cmd_result = FW_RES_RUNNING; return FW_RES_RUNNING; }
    return fw_axis_start_legacy(&s_ax[0]);
}
static void op_legacy_reset(void *ctx) { (void)ctx; fw_axis_legacy_reset(&s_ax[0]); }
static void op_user_do(void *ctx, uint16_t mask) { (void)ctx; fw_hal_user_do_set(mask); }
static void op_cfg_changed(void *ctx) { (void)ctx; fw_save_note_write(&s_save, fw_hal_ms()); }

/* D-088: X/Y simultaneous start from each axis's operand registers (target_position, move_speed, accel, decel).
 * All-or-nothing: if any axis fails the pre-check nothing starts; if a real start then fails, the axes already
 * started in this pass are quick-stopped. Both starts happen in the same motion-task pass (under the lock).
 * Review M-01: an axis with a latched STOP refuses (0x10) instead of releasing the latch here, so a refused
 * simultaneous start never leaves one axis released; the latch is released by that axis's own start (D-072). */
static uint16_t start_all(bool absolute)
{
    if (start_blocked()) return FW_SRES_AXIS_RUNNING;
    bool ok = true;
    for (unsigned a = 0; a < s_dev.axis_count; ++a) {
        uint8_t r = fw_axis_start_check(&s_ax[a], absolute);
        if (r == FW_RES_OK && fw_hal_stop_latched(a)) r = FW_RES_ESTOP;
        if (r != FW_RES_OK) { s_dev.ax[a].cmd_result = r; ok = false; }
    }
    if (!ok) return FW_SRES_REFUSED;
    const uint16_t cmd = absolute ? FW_CMD_START_ABS : FW_CMD_START_REL;
    for (unsigned a = 0; a < s_dev.axis_count; ++a) {
        if (fw_axis_command(&s_ax[a], cmd) == FW_RES_OK) continue;
        for (unsigned b = 0; b < a; ++b) fw_axis_command(&s_ax[b], FW_CMD_QUICK_STOP);
        return FW_SRES_REFUSED;
    }
    return FW_SRES_OK;
}

static uint16_t op_sys_cmd(void *ctx, uint16_t cmd)
{
    (void)ctx;
    const bool locked = !fw_dev_unlocked(&s_dev);
    switch (cmd) {
    case FW_SYS_ESTOP_RELEASE:
        return FW_SRES_REFUSED;                  /* D-058: no reset command; release is a START edge after STOP is inactive */
    case FW_SYS_SAVE:
        if (any_running()) return FW_SRES_AXIS_RUNNING;
        fw_save_request(&s_save);
        return FW_SRES_RUNNING_CMD;
    case FW_SYS_NET_APPLY:
        if (locked) return FW_SRES_LOCKED;
        if (any_running()) return FW_SRES_AXIS_RUNNING;
        fw_save_request(&s_save);
        return FW_SRES_RUNNING_CMD;
    case FW_SYS_FACTORY_RESET:
    case FW_SYS_NET_RESET: {
        const uint16_t r = fw_sys_reset_cmd(&s_dev, cmd, any_running());     /* FR-035: lock + no running axis, logic in fw_core */
        if (r == FW_SRES_RUNNING_CMD) { fw_save_note_write(&s_save, fw_hal_ms()); fw_save_request(&s_save); }
        return r;
    }
    case FW_SYS_LOAD_DEFAULT:
        if (locked) return FW_SRES_LOCKED;
        if (any_running()) return FW_SRES_AXIS_RUNNING;
        fw_cfg_defaults_model(&s_dev.cfg, s_dev.model_rtu);      /* D-059: RTU model keeps RTU */
        fw_save_request(&s_save);
        return FW_SRES_RUNNING_CMD;
    case FW_SYS_REBOOT:
        if (locked) return FW_SRES_LOCKED;
        if (any_running()) return FW_SRES_AXIS_RUNNING;
        s_reboot_req = true; s_reboot_at_ms = fw_hal_ms() + 300u;      /* let the response go out */
        return FW_SRES_OK;
    case FW_SYS_CLEAR_HIST:
        s_dev.hist_count = 0;
        return FW_SRES_OK;
    case FW_SYS_STOP_ALL:
        ip_stop(false);                                                 /* D-089: decelerate along the path */
        for (unsigned a = 0; a < s_dev.axis_count; ++a) fw_axis_command(&s_ax[a], FW_CMD_STOP);
        return FW_SRES_OK;
    case FW_SYS_INTERP_START:                    /* I-049: the path maths (libm double) runs in the motion task, not in */
        s_ip_start_req = true;                   /* the Modbus client task that called this; result follows next pass */
        return FW_SRES_RUNNING_CMD;
    case FW_SYS_START_ALL_REL:
    case FW_SYS_START_ALL_ABS:
        return start_all(cmd == FW_SYS_START_ALL_ABS);
    default: return FW_SRES_REFUSED;
    }
}

/* ------------------------------------------------------------------ START edge from the HAL watch task */
static void on_start_edge(uint8_t axis) { __atomic_fetch_or(&s_start_pending, (uint8_t)(1u << axis), __ATOMIC_SEQ_CST); }
/* D-088: external X/Y simultaneous START (bit 0) / STOP (bit 1) edges, handled in the next motion-task pass */
static void on_sync_edge(bool start) { __atomic_fetch_or(&s_sync_pending, (uint8_t)(start ? 1u : 2u), __ATOMIC_SEQ_CST); }

/* ------------------------------------------------------------------ save
 * Called after motion_task set s_saving under the lock (save_now) or, for the reboot path, with s_reboot_commit set. */
static void do_save(void)
{
    fw_cfg_t img;
    fw_motion_lock();
    s_dev.cfg.version = FW_CFG_VERSION;
    fw_cfg_seal(&s_dev.cfg);
    img = s_dev.cfg;
    s_saving = true;
    fw_save_done(&s_save);
    fw_motion_unlock();
    const int r = fw_hal_store_write(&img, sizeof(img));      /* not under the lock: HMI requests keep being served */
    fw_motion_lock();
    s_saving = false;
    if (r == FW_ST_OK) {
        s_dev.sys_status &= (uint16_t)~FW_SSB_NVM_ERR;
        if (s_dev.sys_cmd_result == FW_SRES_RUNNING_CMD) s_dev.sys_cmd_result = FW_SRES_OK;
        if (!(s_dev.boot_status & FW_BOOT_CFG_OK)) {             /* E-PARAM cleared by "reset parameters + SAVE" */
            s_dev.boot_status |= FW_BOOT_CFG_OK;
            update_oe();
        }
        ESP_LOGI(TAG, "configuration saved");
    } else {
        s_dev.sys_status |= FW_SSB_NVM_ERR;
        fw_hist_push(&s_dev, FW_AL_NVM, 0);
        if (s_dev.sys_cmd_result == FW_SRES_RUNNING_CMD) s_dev.sys_cmd_result = FW_SRES_REFUSED;
        ESP_LOGE(TAG, "configuration save FAILED (E-NVM)");
    }
    fw_motion_unlock();
}

/* ------------------------------------------------------------------ task */
static void refresh_system(uint32_t now)
{
    s_dev.now_ms = now;
    s_dev.uptime_s = now / 1000u;
    if ((uint32_t)(now - s_hb_ms) >= 100u) { s_hb_ms += 100u; s_dev.heartbeat++; }
    uint16_t st = 0, es = 0, alarm_mask = 0, inp = 0;
    for (unsigned a = 0; a < s_dev.axis_count; ++a) {
        if (fw_hal_stop_input((uint8_t)a)) { st |= FW_SSB_ESTOP_IN; inp |= 1u; }
        if (fw_hal_stop_latched((uint8_t)a)) st |= FW_SSB_ESTOP_LATCH;
        const uint16_t x = fw_hal_stop_state((uint8_t)a);
        if (x > es) es = x;
        if (s_dev.ax[a].alarm_code) { alarm_mask |= (uint16_t)(1u << a); st |= FW_SSB_ALARM; }
    }
    if (s_save.dirty) st |= FW_SSB_UNSAVED;
    if (s_saving) st |= FW_SSB_SAVING;
    st |= (uint16_t)(s_dev.sys_status & FW_SSB_NVM_ERR);
    if (s_cl_tripped) st |= FW_SSB_COMMLOSS;
    s_dev.sys_status = st;
    s_dev.estop_state = es;
    s_dev.alarm_axis_mask = alarm_mask;
    if (fw_hal_oe_allowed()) inp |= 2u;                       /* b1: gate state as driven (no read-back hardware) */
    s_dev.sys_inputs = inp;
    s_dev.di_raw = fw_hal_di_raw();
    s_dev.do_state = fw_hal_do_state();
}

static void motion_task(void *arg)
{
    (void)arg;
    if (fw_hal_wdt_add_self() != 0) {                                  /* m-02: unsupervised operation is not allowed */
        fw_motion_lock();
        s_dev.boot_status &= (uint16_t)~FW_BOOT_WDT;
        update_oe();
        fw_motion_unlock();
        ESP_LOGE(TAG, "task watchdog subscription FAILED: pulses forbidden");
    }
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        fw_hal_wdt_feed();
        bool save_now = false;
        uint8_t keep = 0;
        fw_motion_lock();
        const uint32_t now = fw_hal_ms();
        s_dev.now_ms = now;
        const uint8_t pend = __atomic_exchange_n(&s_start_pending, 0, __ATOMIC_SEQ_CST);
        for (unsigned a = 0; a < s_dev.axis_count; ++a) {
            if (!(pend & (1u << a))) continue;
            if (s_ip.active) s_dev.ax[a].cmd_result = FW_RES_RUNNING;   /* D-089: dropped, not deferred past the path */
            else if (start_blocked()) keep |= (uint8_t)(1u << a);      /* m-04: not dropped, retried after the save */
            else fw_axis_start_physical(&s_ax[a]);
        }
        if (keep) __atomic_fetch_or(&s_start_pending, keep, __ATOMIC_SEQ_CST);
        const uint8_t sync = __atomic_exchange_n(&s_sync_pending, 0, __ATOMIC_SEQ_CST);
        if (sync & 2u) s_dev.sys_cmd_result = op_sys_cmd(NULL, FW_SYS_STOP_ALL);          /* STOP wins over START */
        else if (sync & 1u) {
            if (s_ip.active) s_dev.sys_cmd_result = FW_SRES_AXIS_RUNNING;                 /* D-089: dropped */
            else if (start_blocked()) __atomic_fetch_or(&s_sync_pending, 1u, __ATOMIC_SEQ_CST);   /* retried after the save */
            else s_dev.sys_cmd_result = start_all(false);                              /* external input: relative */
        }
        if (s_ip_start_req) { s_ip_start_req = false; s_dev.sys_cmd_result = ip_start(); }   /* I-049 */
        ip_tick(now);                                                     /* D-089: before the axes book their pulses */
        for (unsigned a = 0; a < s_dev.axis_count; ++a) fw_axis_poll(&s_ax[a]);

        const uint16_t *sys = s_dev.cfg.sys;
        const bool running = any_running();
        if (fw_commloss_check(&s_cl, sys[FW_S_COMMLOSS_EN] != 0, sys[FW_S_COMMLOSS_MS], sys[FW_S_COMMLOSS_SCOPE], running, now)) {
            if (s_ip.active) { ip_stop(false); s_ip.commloss = true; }   /* M-02: alarm after the path decel (ip_finish) */
            else for (unsigned a = 0; a < s_dev.axis_count; ++a) fw_axis_decel_all_reason(&s_ax[a], FW_AL_COMMLOSS);
            s_cl_tripped = true;
        }
        if (s_cl_tripped) {                                           /* m-06: cleared when no axis holds the comm-loss alarm */
            bool any = false;
            for (unsigned a = 0; a < s_dev.axis_count; ++a) any = any || fw_axis_sticky_alarm(&s_ax[a]) == FW_AL_COMMLOSS;
            s_cl_tripped = any;
        }
        refresh_system(now);
        if (fw_nrhold_update(&s_nrhold, (s_dev.di_raw & 1u) != 0, (s_dev.di_raw & 2u) != 0, (s_dev.boot_status & FW_BOOT_STOP_IO) != 0, now)) {
            ESP_LOGW(TAG, "boot hold START+STOP 10 s then released: network settings back to factory values, rebooting");
            fw_hal_nrreset_mark();                                    /* once only: the next boot ignores the hold */
            fw_cfg_net_reset(&s_dev.cfg, s_dev.model_rtu);
            fw_save_note_write(&s_save, now);                         /* dirty: the reboot path saves it first */
            fw_save_request(&s_save);
            s_reboot_req = true; s_reboot_at_ms = now;
        }
        if (fw_save_due(&s_save, now, sys[FW_S_AUTO_SAVE] != 0, running)) { save_now = true; s_saving = true; }
        const bool do_reboot = s_reboot_req && (int32_t)(now - s_reboot_at_ms) >= 0 && !running;
        if (do_reboot) s_reboot_commit = true;                         /* decided under the lock: no START from here on */
        fw_motion_unlock();

        if (save_now) do_save();
        if (do_reboot) {
            if (s_save.dirty) do_save();
            ESP_LOGW(TAG, "REBOOT command");
            fw_hal_reboot();
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(PERIOD_MS));
    }
}

size_t fw_motion_process_pdu(int32_t conn, const uint8_t *req, size_t len, uint8_t *resp)
{
    fw_motion_lock();
    s_dev.now_ms = fw_hal_ms();
    fw_commloss_activity(&s_cl, conn, fw_mb_is_command(req, len), s_dev.now_ms);
    const size_t n = fw_mb_process(&s_dev, req, len, resp);
    fw_motion_unlock();
    return n;
}

void fw_motion_conn_closed(int32_t conn)
{
    fw_motion_lock(); fw_commloss_conn_closed(&s_cl, conn); fw_motion_unlock();
}

/* ------------------------------------------------------------------ boot */
int fw_motion_start(const fw_motion_cfg_t *c)
{
    s_lock = xSemaphoreCreateMutex();
    fw_dev_init(&s_dev, c->axis_count, c->model_rtu);
    s_dev.fw_version = c->fw_version;
    s_dev.ops = (fw_dev_ops_t){op_axis_cmd, op_legacy_start, op_legacy_reset, op_sys_cmd, op_user_do, op_cfg_changed, NULL};
    fw_commloss_init(&s_cl, fw_hal_ms());
    fw_nrhold_init(&s_nrhold, fw_hal_nrreset_take());
    s_hb_ms = fw_hal_ms();

    /* reset cause (T-15) */
    s_dev.reset_reason = fw_hal_reset_reason();
    if (fw_rst_is_wdt(s_dev.reset_reason)) fw_hist_push(&s_dev, FW_AL_WDT, 0);
    ESP_LOGI(TAG, "reset reason code %u", (unsigned)s_dev.reset_reason);

    /* watchdog first: if it cannot be armed the system does not operate */
    if (fw_hal_wdt_init() == 0) s_dev.boot_status |= FW_BOOT_WDT; else ESP_LOGE(TAG, "task watchdog init FAILED: operation prohibited");

    /* configuration */
    if (fw_hal_store_init() == 0) {
        s_dev.boot_status |= FW_BOOT_NVS;
        fw_cfg_t img; size_t len = sizeof(img);
        const int r = fw_hal_store_read(&img, &len);
        if (r == FW_ST_OK) {
            const fw_cfg_check_t chk = fw_cfg_verify(&img, len);
            if (chk == FW_CFG_OK) { s_dev.cfg = img; s_dev.boot_status |= FW_BOOT_CFG_OK; }
            else {
                ESP_LOGE(TAG, "stored configuration invalid (%d): defaults loaded, operation prohibited until SAVE (E-PARAM)", (int)chk);
                fw_hist_push(&s_dev, FW_AL_PARAM, 0);                       /* defaults already in s_dev.cfg */
            }
        } else if (r == FW_ST_BAD_SIZE) {
            ESP_LOGE(TAG, "stored configuration has the wrong size: defaults loaded, operation prohibited until SAVE (E-PARAM)");
            fw_hist_push(&s_dev, FW_AL_PARAM, 0);
        } else if (r == FW_ST_NOT_FOUND) {
            ESP_LOGI(TAG, "no stored configuration: defaults (first boot)");
            s_dev.boot_status |= FW_BOOT_CFG_OK;
            fw_save_request(&s_save);
        } else {
            ESP_LOGE(TAG, "NVS read failed: defaults loaded, operation prohibited");
            s_dev.boot_status &= (uint16_t)~FW_BOOT_NVS;
        }
    } else {
        ESP_LOGE(TAG, "NVS init FAILED: operation prohibited");
    }
    bool fell = false;
    s_dev.comm_mode_active = fw_mode_effective(s_dev.cfg.net[FW_N_COMM_MODE], c->model_rtu, &fell);
    if (fell) { s_dev.boot_status |= FW_BOOT_MODE_FELLBACK; ESP_LOGW(TAG, "stored RTU mode refused on this model: TCP"); }

    /* hardware */
    uint32_t res[2] = {fw_w32(s_dev.cfg.par[0], FW_P_PULSE_RES), fw_w32(s_dev.cfg.par[1], FW_P_PULSE_RES)};
    const int pe = fw_hal_pulse_init(c->axis_count, res);
    for (unsigned a = 0; a < c->axis_count; ++a)
        if (fw_hal_pulse_ready((uint8_t)a)) s_dev.boot_status |= (a == 0 ? FW_BOOT_PULSE_X : FW_BOOT_PULSE_Y);
    if (pe != 0) ESP_LOGE(TAG, "pulse engine init error 0x%x", (unsigned)pe);
    fw_hal_set_start_cb(on_start_edge);                                    /* m-04: before the io task exists */
    fw_hal_set_sync_cb(on_sync_edge);                                      /* D-088 */
    if (fw_hal_io_init(c->axis_count) == 0) s_dev.boot_status |= FW_BOOT_STOP_IO;
    else ESP_LOGE(TAG, "STOP/START input init FAILED: operation prohibited");
    for (unsigned a = 0; a < c->axis_count; ++a) {                         /* M-06: fault path of each axis */
        if (fw_hal_fault_bound((uint8_t)a)) s_dev.boot_status |= (a == 0 ? FW_BOOT_FAULT_X : FW_BOOT_FAULT_Y);
        else ESP_LOGE(TAG, "axis %u: MCPWM fault path not bound: axis operation PROHIBITED (alarm E-PE-HW)", (unsigned)(a + 1));
    }

    for (unsigned a = 0; a < c->axis_count; ++a) fw_axis_init(&s_ax[a], &s_dev, (uint8_t)a, res[a]);
    const int allowed = update_oe();
    if (allowed == 0) for (unsigned a = 0; a < c->axis_count; ++a) fw_axis_servo_boot(&s_ax[a]);
    for (unsigned a = 0; a < c->axis_count; ++a) fw_axis_poll(&s_ax[a]);
    refresh_system(fw_hal_ms());
    (void)fw_nrhold_update(&s_nrhold, (s_dev.di_raw & 1u) != 0, (s_dev.di_raw & 2u) != 0, (s_dev.boot_status & FW_BOOT_STOP_IO) != 0, fw_hal_ms());   /* boot sample (D-076) */
    ESP_LOGI(TAG, "boot_status 0x%04x, pulses %s", (unsigned)s_dev.boot_status, allowed == 0 ? "ALLOWED" : "PROHIBITED");

    if (xTaskCreate(motion_task, "fw_motion", 4096, NULL, 10, NULL) != pdPASS) {
        fw_hal_oe_forbid();
        ESP_LOGE(TAG, "motion task could not be created: pulses forbidden");
        return -2;
    }
    return allowed;
}
