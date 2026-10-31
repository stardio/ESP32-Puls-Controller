/* fw_core / fw_params: persisted configuration image, parameter tables with range checks, communication mode
 * selection, deferred-save scheduler, comm-loss monitor, pin conflict check. Hardware independent (host-testable).
 * Ranges/defaults follow interface-spec sec.5.3-5.10 (regmap v0.2); [assumed] values are the spec's own. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "fw_profile.h"

#define FW_MAX_AXES 2

/* ---- register word counts of the persisted segments ---- */
#define FW_PAR_WORDS 0x2C          /* B+0x100..0x12B */
#define FW_OP_WORDS  0x10          /* B+0x040..0x04F (registers 0x042..0x049 defined) */
#define FW_SYS_WORDS 5             /* 0x0110..0x0114 */
#define FW_NET_WORDS 0x10          /* 0x4000..0x400F */
#define FW_LEG_WORDS 4             /* legacy_move_pulses (2 words), flags, spare */

/* entry flags */
#define FWE_SIGNED  1u
#define FWE_PERSIST 2u            /* part of the NVS image */
typedef struct { uint16_t off; uint8_t words; uint8_t flags; int64_t min, max, def; } fw_ent_t;
typedef struct { const fw_ent_t *e; unsigned n; unsigned nwords; } fw_seg_t;

extern const fw_seg_t FW_SEG_PAR, FW_SEG_OP, FW_SEG_SYS, FW_SEG_NET;

/* parameter offsets inside the parameter segment (register = B + 0x100 + offset) */
enum {
    FW_P_PULSE_MODE = 0x00, FW_P_PULSE_LOGIC = 0x01, FW_P_DIR_INVERT = 0x02, FW_P_DIR_SETUP_US = 0x03,
    FW_P_MAX_SPEED = 0x04, FW_P_START_SPEED = 0x06, FW_P_DEF_ACCEL = 0x08, FW_P_DEF_DECEL = 0x09,
    FW_P_ACCEL_CURVE = 0x0A, FW_P_S_RATIO = 0x0B, FW_P_JOG_SPEED = 0x0C, FW_P_JOG_MAX = 0x0E, FW_P_JOG_ACCEL = 0x10,
    FW_P_HOME_METHOD = 0x11, FW_P_HOME_DIR = 0x12, FW_P_HOME_HIGH = 0x13, FW_P_HOME_CREEP = 0x15,
    FW_P_HOME_OFFSET = 0x17, FW_P_HOME_TIMEOUT = 0x19, FW_P_SWLIM_EN = 0x1A, FW_P_SWLIM_POS = 0x1B,
    FW_P_SWLIM_NEG = 0x1D, FW_P_INPUT_LOGIC = 0x1F, FW_P_INPUT_FILTER = 0x20, FW_P_LIMIT_STOP = 0x21,
    FW_P_LIMIT_DECEL = 0x22, FW_P_ALARM_STOP = 0x23, FW_P_INP_WAIT = 0x24, FW_P_INP_TIMEOUT = 0x25,
    FW_P_SVON_AT_BOOT = 0x26, FW_P_ESTOP_POS = 0x27, FW_P_REQUIRE_HOME = 0x28, FW_P_SVON_MODE = 0x29,
    FW_P_PULSE_RES = 0x2A
};
/* op segment offsets (register = B + 0x040 + offset) */
enum { FW_O_COMMAND = 0x00, FW_O_TARGET = 0x02, FW_O_SPEED = 0x04, FW_O_ACCEL = 0x06, FW_O_DECEL = 0x07, FW_O_JOGOVR = 0x08 };
/* sys segment offsets (register = 0x0110 + offset) */
enum { FW_S_COMMLOSS_EN = 0, FW_S_COMMLOSS_MS, FW_S_COMMLOSS_SCOPE, FW_S_AUTO_SAVE, FW_S_MAX_CLIENTS };
/* net segment offsets (register = 0x4000 + offset) */
/* FW_N_RTU_BAUD holds baud / 100 (96 = 9600, 1152 = 115200) */
enum { FW_N_IP = 0, FW_N_MASK = 2, FW_N_GW = 4, FW_N_UNIT = 6, FW_N_MAC = 7 /*3 words, read-only*/, FW_N_RTU_BAUD = 0x0A,
       FW_N_RTU_PARITY = 0x0B, FW_N_RTU_ADDR = 0x0C, FW_N_COMM_MODE = 0x0E };

/* ---- persisted image (RAM copy = live register values) ---- */
#define FW_CFG_VERSION 1u
typedef struct {
    uint32_t version;
    uint16_t leg[FW_LEG_WORDS];                  /* [0..1] legacy_move_pulses (low word first), [2] b0 = direction forward */
    uint16_t sys[FW_SYS_WORDS];
    uint16_t net[FW_NET_WORDS];
    uint16_t op[FW_MAX_AXES][FW_OP_WORDS];
    uint16_t par[FW_MAX_AXES][FW_PAR_WORDS];
    uint32_t crc;                                /* fw_crc32 over everything before this field */
} fw_cfg_t;

/* generic accessors on a word array */
FW_INLINE uint32_t fw_w32(const uint16_t *w, unsigned off) { return (uint32_t)w[off] | ((uint32_t)w[off + 1] << 16); }
FW_INLINE void fw_set32(uint16_t *w, unsigned off, uint32_t v) { w[off] = (uint16_t)v; w[off + 1] = (uint16_t)(v >> 16); }

void fw_cfg_defaults_model(fw_cfg_t *c, bool model_rtu);   /* D-059: defaults + comm_mode = RTU on the RTU model */
void fw_cfg_defaults(fw_cfg_t *c);               /* all tables' defaults; op accel/decel from default_accel/decel */
void fw_cfg_seal(fw_cfg_t *c);                   /* set version + crc */
typedef enum { FW_CFG_OK = 0, FW_CFG_BAD_SIZE, FW_CFG_BAD_VERSION, FW_CFG_BAD_CRC, FW_CFG_BAD_RANGE } fw_cfg_check_t;
fw_cfg_check_t fw_cfg_verify(const fw_cfg_t *c, size_t stored_size);     /* CRC, version AND every range */

/* range check of one entry value and of a whole segment image (incl. cross-field rules); returns 0 or the offset+1
 * of the first offending entry */
bool     fw_ent_check(const fw_ent_t *e, int64_t v);
const fw_ent_t *fw_seg_find(const fw_seg_t *s, unsigned off);            /* entry whose words cover off, or NULL */
unsigned fw_par_validate(const uint16_t *par);   /* 0 ok, else (offset+1) of the first failing parameter */
unsigned fw_op_validate(const uint16_t *op, const uint16_t *par, bool via_axis_area);
unsigned fw_sys_validate(const uint16_t *sys);
unsigned fw_net_validate(const uint16_t *net, bool model_rtu);
int64_t  fw_ent_value(const fw_ent_t *e, const uint16_t *w);

/* ---- communication mode (D-056, D-059) ---- */
enum { FW_MODE_TCP = 0, FW_MODE_RTU = 1 };
/* A write of the mode register: RTU only on a model with RS-485 hardware. 0 = accepted */
bool fw_mode_write_ok(uint16_t requested, bool model_rtu);
/* Mode actually used after boot from the stored value: invalid or RTU-without-hardware falls back to TCP.
 * *fell_back is set when the stored value had to be replaced. */
uint16_t fw_mode_effective(uint16_t stored, bool model_rtu, bool *fell_back);

/* ---- deferred save (no NVS write per register write, no write while an axis runs) ---- */
#define FW_AUTO_SAVE_DELAY_MS 2000u   /* interface-spec 5.1 [assumed] */
typedef struct { bool dirty; bool save_req; uint32_t last_write_ms; } fw_save_t;
void fw_save_note_write(fw_save_t *s, uint32_t now_ms);
void fw_save_request(fw_save_t *s);                         /* SAVE command */
/* true when the caller must write now. Never while any axis runs. */
bool fw_save_due(const fw_save_t *s, uint32_t now_ms, bool auto_save, bool any_running);
void fw_save_done(fw_save_t *s);

/* ---- comm-loss monitor (FR-012: reg 0x0110..0x0112) ---- */
typedef struct { uint32_t last_any_ms; uint32_t last_cmd_ms; int32_t cmd_conn; bool cmd_conn_open; } fw_commloss_t;
void fw_commloss_init(fw_commloss_t *c, uint32_t now_ms);
void fw_commloss_activity(fw_commloss_t *c, int32_t conn, bool is_command, uint32_t now_ms);
void fw_commloss_conn_closed(fw_commloss_t *c, int32_t conn);
/* true when moving axes must decelerate to a stop */
bool fw_commloss_check(const fw_commloss_t *c, bool enable, uint32_t time_ms, uint16_t scope, bool running, uint32_t now_ms);

/* ---- pins (review M-08, F-08): true if any pin of a[] (>= 0) appears twice in a[] or also in b[] (>= 0) ---- */
bool fw_pin_conflict(const int *a, size_t na, const int *b, size_t nb);

/* ---- misc codes ---- */
enum {   /* reset_reason register 0x002C */
    FW_RST_UNKNOWN = 0, FW_RST_POWERON = 1, FW_RST_SOFTWARE = 2, FW_RST_PANIC = 3, FW_RST_INT_WDT = 4,
    FW_RST_TASK_WDT = 5, FW_RST_OTHER_WDT = 6, FW_RST_BROWNOUT = 7, FW_RST_DEEPSLEEP = 8, FW_RST_EXT = 9, FW_RST_OTHER = 10
};
FW_INLINE bool fw_rst_is_wdt(uint16_t r) { return r == FW_RST_INT_WDT || r == FW_RST_TASK_WDT || r == FW_RST_OTHER_WDT; }
