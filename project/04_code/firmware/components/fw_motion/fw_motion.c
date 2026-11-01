#include "fw_motion.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "fw_axis.h"
#include "fw_cfg.h"
#include "fw_hal.h"

static const char *TAG = "FW_MOTION";
#define PERIOD_MS CONFIG_FW_UPDATE_MS

static fw_dev_t s_dev;
static fw_axis_t s_ax[FW_MAX_AXES];
static SemaphoreHandle_t s_lock;
static fw_save_t s_save;
static fw_commloss_t s_cl;
static volatile uint8_t s_start_pending;
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

/* review M-03: from the moment motion_task decides to save / reboot (under the lock) no start is accepted */
static bool start_blocked(void) { return s_saving || s_reboot_commit; }

static bool any_running(void)
{
    for (unsigned a = 0; a < s_dev.axis_count; ++a) if (fw_axis_running(&s_ax[a])) return true;
    return false;
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
        for (unsigned a = 0; a < s_dev.axis_count; ++a) fw_axis_command(&s_ax[a], FW_CMD_STOP);
        return FW_SRES_OK;
    default: return FW_SRES_REFUSED;
    }
}

/* ------------------------------------------------------------------ START edge from the HAL watch task */
static void on_start_edge(uint8_t axis) { __atomic_fetch_or(&s_start_pending, (uint8_t)(1u << axis), __ATOMIC_SEQ_CST); }

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
            if (start_blocked()) keep |= (uint8_t)(1u << a);          /* m-04: not dropped, retried after the save */
            else fw_axis_start_physical(&s_ax[a]);
        }
        if (keep) __atomic_fetch_or(&s_start_pending, keep, __ATOMIC_SEQ_CST);
        for (unsigned a = 0; a < s_dev.axis_count; ++a) fw_axis_poll(&s_ax[a]);

        const uint16_t *sys = s_dev.cfg.sys;
        const bool running = any_running();
        if (fw_commloss_check(&s_cl, sys[FW_S_COMMLOSS_EN] != 0, sys[FW_S_COMMLOSS_MS], sys[FW_S_COMMLOSS_SCOPE], running, now)) {
            for (unsigned a = 0; a < s_dev.axis_count; ++a) fw_axis_decel_all_reason(&s_ax[a], FW_AL_COMMLOSS);
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
