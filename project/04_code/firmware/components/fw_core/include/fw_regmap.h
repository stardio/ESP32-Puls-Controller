/* fw_core / fw_regmap: device model + Modbus PDU processing for the TCP/RTU-common register map
 * (interface-spec sec.5, regmap v0.2). No transport, no hardware: the transports call fw_mb_process() with the
 * device lock held; commands go out through fw_dev_ops_t. Host-testable.
 * Source for the legacy window and the PDU framing: servo1axis movion_modbus_tcp.c (process_request). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "fw_params.h"
#include "fw_unit.h"

#define FW_REGMAP_VERSION   2u
#define FW_UNLOCK_KEY       0x4D56u       /* [assumed] fixed key value of unlock_key (0x0102) */
#define FW_UNLOCK_MS        10000u
#define FW_HIST_N           32u
#define FW_MB_MAX_PDU       253

/* axis_command (B+0x040) */
enum { FW_CMD_START_ABS = 1, FW_CMD_START_REL = 2, FW_CMD_STOP = 3, FW_CMD_HOME = 4, FW_CMD_ALARM_RESET = 5,
       FW_CMD_SERVO_ON = 6, FW_CMD_SERVO_OFF = 7, FW_CMD_JOG_POS = 8, FW_CMD_JOG_NEG = 9, FW_CMD_JOG_STOP = 10,
       FW_CMD_SET_POSITION = 11, FW_CMD_QUICK_STOP = 12,
       /* interface-spec 5.12 */
       FW_CMD_START_ABS_UNIT = 13, FW_CMD_START_REL_UNIT = 14, FW_CMD_TEST_POS = 15, FW_CMD_TEST_NEG = 16,
       FW_CMD_POINT_GO = 17, FW_CMD_POINT_TEACH = 18 };
#define FW_TEST_KEEPALIVE_MS 500u   /* TEST_POS/NEG must be rewritten within this time, else decel stop (FR-032) */
/* cmd_result */
enum { FW_RES_OK = 0, FW_RES_BUSY_RUN = 1, FW_RES_ESTOP = 0x10, FW_RES_ALARM = 0x11, FW_RES_SERVO_OFF = 0x12,
       FW_RES_RUNNING = 0x13, FW_RES_SWLIMIT = 0x14, FW_RES_LIMIT_DIR = 0x15, FW_RES_POS_INVALID = 0x16,
       FW_RES_RANGE = 0x17 };
/* sys_command (0x0100) / sys_cmd_result (0x0101) */
enum { FW_SYS_ESTOP_RELEASE = 1, FW_SYS_SAVE = 2, FW_SYS_LOAD_DEFAULT = 3, FW_SYS_REBOOT = 4, FW_SYS_CLEAR_HIST = 5,
       FW_SYS_STOP_ALL = 6, FW_SYS_FACTORY_RESET = 7, FW_SYS_NET_RESET = 8, FW_SYS_NET_APPLY = 0x100 /* internal: write of 0x400F */ };
enum { FW_SRES_OK = 0, FW_SRES_RUNNING_CMD = 1, FW_SRES_REFUSED = 0x10, FW_SRES_LOCKED = 0x11, FW_SRES_AXIS_RUNNING = 0x12 };
/* axis_state */
enum { FW_AS_SERVO_OFF = 0, FW_AS_IDLE = 1, FW_AS_POSITIONING = 2, FW_AS_JOG = 3, FW_AS_HOME = 4, FW_AS_DECEL_STOP = 5,
       FW_AS_ALARM = 6, FW_AS_ESTOP = 7 };
/* axis_status bits */
enum { FW_ASB_READY = 1u << 0, FW_ASB_BUSY = 1u << 1, FW_ASB_DONE = 1u << 2, FW_ASB_HOME_DONE = 1u << 3, FW_ASB_ALARM = 1u << 4,
       FW_ASB_ESTOP = 1u << 5, FW_ASB_SERVO = 1u << 10, FW_ASB_POS_INVALID = 1u << 11, FW_ASB_COMMLOSS = 1u << 14 };
/* sys_status bits */
enum { FW_SSB_ESTOP_IN = 1u << 0, FW_SSB_ESTOP_LATCH = 1u << 1, FW_SSB_ALARM = 1u << 2, FW_SSB_UNSAVED = 1u << 3,
       FW_SSB_SAVING = 1u << 4, FW_SSB_NVM_ERR = 1u << 5, FW_SSB_COMMLOSS = 1u << 6 };
/* boot_status (0x002D): a 1 bit means that part initialised OK; motion is allowed only when all REQUIRED bits are set */
enum { FW_BOOT_PULSE_X = 1u << 0, FW_BOOT_PULSE_Y = 1u << 1, FW_BOOT_STOP_IO = 1u << 2, FW_BOOT_NVS = 1u << 3,
       FW_BOOT_CFG_OK = 1u << 4, FW_BOOT_WDT = 1u << 5, FW_BOOT_FAULT_X = 1u << 6, FW_BOOT_FAULT_Y = 1u << 7 /* review M-06: MCPWM fault path of that axis bound */,
       FW_BOOT_MODE_FELLBACK = 1u << 8, FW_BOOT_OE_ALLOWED = 1u << 9 };
/* alarm codes (5.8) */
enum { FW_AL_NONE = 0, FW_AL_ESTOP = 0x0101, FW_AL_COMMLOSS = 0x0401, FW_AL_PE_HW = 0x0602, FW_AL_PE_FAULTPATH = 0x0603, FW_AL_PARAM = 0x0701,
       FW_AL_NVM = 0x0702, FW_AL_WDT = 0x0801 };
#define FW_WIRING_VALID (1u << 15)
/* stop causes */
enum { FW_SC_DONE = 1, FW_SC_STOP_CMD = 2, FW_SC_QUICK = 3 };

typedef struct {
    uint16_t state, status, alarm_code, last_stop_cause, cmd_result, axis_inputs, alarm_count;
    int32_t  cmd_position, cmd_speed, target_position_act;
    uint32_t move_count, pulses_done;
    uint16_t leg_result, leg_fault;          /* legacy 4x-14 / 4x-15 */
    bool     moving, last_ok, servo_manual;
    bool     loop_seen;                  /* PCNT loopback counted pulses of the last move (wiring_status b2/b6) */
} fw_axis_tele_t;

typedef struct {
    uint8_t  (*axis_cmd)(void *ctx, uint8_t axis, uint16_t cmd);   /* returns cmd_result; also stores it in tele */
    uint8_t  (*legacy_start)(void *ctx);                           /* coil 0: relative move of axis X from the legacy values */
    void     (*legacy_reset)(void *ctx);                           /* coil 2 extra: clear current value, result, fault */
    uint16_t (*sys_cmd)(void *ctx, uint16_t cmd);                  /* returns sys_cmd_result */
    void     (*user_do)(void *ctx, uint16_t mask);                 /* DO3 (b0) / DO4 (b1) */
    void     (*cfg_changed)(void *ctx);                            /* a persisted value changed (starts the save timer) */
    void     *ctx;
} fw_dev_ops_t;

typedef struct { uint16_t code, axis; uint32_t uptime_s; } fw_hist_t;

typedef struct {
    uint8_t  axis_count;
    bool     model_rtu;
    fw_cfg_t cfg;                       /* live register values (persisted image) */
    fw_axis_tele_t ax[FW_MAX_AXES];
    uint32_t now_ms;                    /* set by the caller before fw_mb_process (unlock timeout) */
    uint32_t fw_version;
    uint16_t sys_status, estop_state, tcp_clients, heartbeat, sys_inputs, alarm_axis_mask;
    uint32_t uptime_s;
    uint16_t reset_reason, boot_status, comm_mode_active, di_raw, do_state, user_do;
    uint16_t sys_cmd_result, coils_hi;
    bool     link;
    bool     rtu_rx_seen; uint32_t rtu_rx_ms;     /* last valid RTU frame (wiring_status b9); set by fw_dev_note_rtu_rx */
    uint8_t  mac[6];
    uint32_t unlock_deadline_ms; bool unlock_active;
    fw_hist_t hist[FW_HIST_N]; uint16_t hist_count;
    fw_dev_ops_t ops;
} fw_dev_t;

void fw_dev_init(fw_dev_t *d, uint8_t axis_count, bool model_rtu);   /* cfg = defaults, ops cleared */
bool fw_dev_unlocked(const fw_dev_t *d);
/* wiring_status (0x0033) and alarm_hint (0x0034), computed from the live model (FR-031, FR-033) */
uint16_t fw_wiring_status(const fw_dev_t *d);
uint16_t fw_alarm_hint(const fw_dev_t *d);
void fw_dev_note_rtu_rx(fw_dev_t *d);                                 /* a valid RTU frame arrived (now_ms stamped) */
/* FACTORY_RESET / NET_RESET (FR-035). Needs the unlock key and no running axis; on success the live image is already
 * replaced and FW_SRES_RUNNING_CMD is returned: the caller must request the NVS save. */
uint16_t fw_sys_reset_cmd(fw_dev_t *d, uint16_t cmd, bool any_axis_running);
void fw_hist_push(fw_dev_t *d, uint16_t code, uint16_t axis);

/* Process one request PDU (function code first). Returns the response PDU length, 0 = no response.
 * Caller holds the device lock for the whole call (32-bit values are read from one snapshot, writes are atomic). */
size_t fw_mb_process(fw_dev_t *d, const uint8_t *req, size_t len, uint8_t *resp);
/* true if the request PDU is a command that can start/stop/modify motion (for the comm-loss monitor) */
bool fw_mb_is_command(const uint8_t *req, size_t len);

/* exposed for tests */
int fw_reg_read(const fw_dev_t *d, uint16_t addr, uint16_t *out);   /* 0 or Modbus exception 2 */
