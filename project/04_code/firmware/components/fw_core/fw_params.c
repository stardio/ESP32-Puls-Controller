#include "fw_params.h"
#include <string.h>

#define U16(o, mn, mx, df)  {(o), 1, FWE_PERSIST, (mn), (mx), (df)}
#define U32(o, mn, mx, df)  {(o), 2, FWE_PERSIST, (mn), (mx), (df)}
#define S32(o, mn, mx, df)  {(o), 2, FWE_PERSIST | FWE_SIGNED, (mn), (mx), (df)}

/* interface-spec 5.6. Ranges are narrowed where v1 firmware implements only one value (see notes/firmware.md):
 * pulse_mode, pulse_logic, accel_curve, swlimit_enable, inp_wait_enable, estop_pos_policy accept only 0. */
static const fw_ent_t PAR[] = {
    U16(FW_P_PULSE_MODE, 0, 0, 0),           U16(FW_P_PULSE_LOGIC, 0, 0, 0),
    U16(FW_P_DIR_INVERT, 0, 1, 0),           U16(FW_P_DIR_SETUP_US, 1, 1000, 10),
    U32(FW_P_MAX_SPEED, 1, FW_SPEED_MAX_HZ, 100000), U32(FW_P_START_SPEED, 0, FW_SPEED_MAX_HZ, 0),
    U16(FW_P_DEF_ACCEL, 1, 65535, 100),      U16(FW_P_DEF_DECEL, 1, 65535, 100),
    U16(FW_P_ACCEL_CURVE, 0, 0, 0),          U16(FW_P_S_RATIO, 0, 100, 50),
    U32(FW_P_JOG_SPEED, 1, FW_SPEED_MAX_HZ, 10000), U32(FW_P_JOG_MAX, 1, FW_SPEED_MAX_HZ, 50000),
    U16(FW_P_JOG_ACCEL, 1, 65535, 100),
    U16(FW_P_HOME_METHOD, 0, 2, 0),          U16(FW_P_HOME_DIR, 0, 1, 1),
    U32(FW_P_HOME_HIGH, 1, FW_SPEED_MAX_HZ, 10000), U32(FW_P_HOME_CREEP, 1, FW_SPEED_MAX_HZ, 500),
    S32(FW_P_HOME_OFFSET, INT32_MIN, INT32_MAX, 0), U16(FW_P_HOME_TIMEOUT, 0, 3600, 60),
    U16(FW_P_SWLIM_EN, 0, 0, 0),
    S32(FW_P_SWLIM_POS, INT32_MIN, INT32_MAX, INT32_MAX), S32(FW_P_SWLIM_NEG, INT32_MIN, INT32_MAX, INT32_MIN),
    U16(FW_P_INPUT_LOGIC, 0, 0x1F, 0x0006),  U16(FW_P_INPUT_FILTER, 0, 20, 1),
    U16(FW_P_LIMIT_STOP, 0, 1, 0),           U16(FW_P_LIMIT_DECEL, 1, 65535, 10),
    U16(FW_P_ALARM_STOP, 0, 1, 0),           U16(FW_P_INP_WAIT, 0, 0, 0),
    U16(FW_P_INP_TIMEOUT, 0, 60000, 3000),   U16(FW_P_SVON_AT_BOOT, 0, 1, 0),
    U16(FW_P_ESTOP_POS, 0, 0, 0),            U16(FW_P_REQUIRE_HOME, 0, 1, 1),
    U16(FW_P_SVON_MODE, 0, 1, 0),            U32(FW_P_PULSE_RES, 2000000, 40000000, 2000000),
};
/* interface-spec 5.5 (op registers are stored so that the legacy window and the axis area share them) */
static const fw_ent_t OP[] = {
    S32(FW_O_TARGET, INT32_MIN, INT32_MAX, 0),
    U32(FW_O_SPEED, FW_SPEED_MIN_HZ, FW_SPEED_MAX_HZ, 10000),
    U16(FW_O_ACCEL, 10, 60000, 400), U16(FW_O_DECEL, 10, 60000, 400),
    U32(FW_O_JOGOVR, 0, FW_SPEED_MAX_HZ, 0),
};
/* interface-spec 5.3 */
static const fw_ent_t SYS[] = {
    U16(FW_S_COMMLOSS_EN, 0, 1, 0), U16(FW_S_COMMLOSS_MS, 100, 60000, 1000), U16(FW_S_COMMLOSS_SCOPE, 0, 1, 0),
    U16(FW_S_AUTO_SAVE, 0, 1, 1), U16(FW_S_MAX_CLIENTS, 1, 8, 4),
};
/* interface-spec 5.10 (+ comm_mode at 0x400E, new, see notes) */
static const fw_ent_t NET[] = {
    U32(FW_N_IP, 1, 0xDFFFFFFFLL, 0xC0A800DCLL),            /* 192.168.0.220 */
    U32(FW_N_MASK, 1, 0xFFFFFFFFLL, 0xFFFFFF00LL),
    U32(FW_N_GW, 0, 0xDFFFFFFFLL, 0),
    U16(FW_N_UNIT, 1, 247, 1),
    U16(FW_N_RTU_BAUD, 12, 1152, 96), U16(FW_N_RTU_PARITY, 0, 2, 0), U16(FW_N_RTU_ADDR, 1, 247, 1),
    U16(FW_N_COMM_MODE, 0, 1, FW_MODE_TCP),
};
#define N(a) (unsigned)(sizeof(a) / sizeof((a)[0]))
const fw_seg_t FW_SEG_PAR = {PAR, N(PAR), FW_PAR_WORDS};
const fw_seg_t FW_SEG_OP  = {OP, N(OP), FW_OP_WORDS};
const fw_seg_t FW_SEG_SYS = {SYS, N(SYS), FW_SYS_WORDS};
const fw_seg_t FW_SEG_NET = {NET, N(NET), FW_NET_WORDS};

const fw_ent_t *fw_seg_find(const fw_seg_t *s, unsigned off)
{
    for (unsigned i = 0; i < s->n; ++i)
        if (off >= s->e[i].off && off < (unsigned)s->e[i].off + s->e[i].words) return &s->e[i];
    return NULL;
}

int64_t fw_ent_value(const fw_ent_t *e, const uint16_t *w)
{
    if (e->words == 1) return (e->flags & FWE_SIGNED) ? (int64_t)(int16_t)w[e->off] : (int64_t)w[e->off];
    const uint32_t u = fw_w32(w, e->off);
    return (e->flags & FWE_SIGNED) ? (int64_t)(int32_t)u : (int64_t)u;
}

bool fw_ent_check(const fw_ent_t *e, int64_t v) { return v >= e->min && v <= e->max; }

static unsigned seg_validate(const fw_seg_t *s, const uint16_t *w)
{
    for (unsigned i = 0; i < s->n; ++i)
        if (!fw_ent_check(&s->e[i], fw_ent_value(&s->e[i], w))) return s->e[i].off + 1u;
    return 0;
}

unsigned fw_par_validate(const uint16_t *p)
{
    unsigned bad = seg_validate(&FW_SEG_PAR, p);
    if (bad) return bad;
    if (!fw_res_valid(fw_w32(p, FW_P_PULSE_RES))) return FW_P_PULSE_RES + 1u;
    const uint32_t maxs = fw_w32(p, FW_P_MAX_SPEED), st = fw_w32(p, FW_P_START_SPEED);
    const uint32_t jm = fw_w32(p, FW_P_JOG_MAX), js = fw_w32(p, FW_P_JOG_SPEED);
    const uint32_t hh = fw_w32(p, FW_P_HOME_HIGH), hc = fw_w32(p, FW_P_HOME_CREEP);
    if (st > maxs) return FW_P_START_SPEED + 1u;                /* 0 (auto) or 1..max_speed */
    if (jm > maxs) return FW_P_JOG_MAX + 1u;
    if (js > jm) return FW_P_JOG_SPEED + 1u;
    if (hh > maxs) return FW_P_HOME_HIGH + 1u;
    if (hc > hh) return FW_P_HOME_CREEP + 1u;
    return 0;
}

unsigned fw_op_validate(const uint16_t *op, const uint16_t *par, bool via_axis_area)
{
    unsigned bad = seg_validate(&FW_SEG_OP, op);
    if (bad) return bad;
    const uint32_t speed = fw_w32(op, FW_O_SPEED);
    if (via_axis_area) {
        const uint32_t st = fw_w32(par, FW_P_START_SPEED);
        if (speed > fw_w32(par, FW_P_MAX_SPEED) || (st && speed < st)) return FW_O_SPEED + 1u;
    }
    const uint32_t jo = fw_w32(op, FW_O_JOGOVR);
    if (jo > fw_w32(par, FW_P_JOG_MAX)) return FW_O_JOGOVR + 1u;
    return 0;
}

unsigned fw_sys_validate(const uint16_t *s) { return seg_validate(&FW_SEG_SYS, s); }

static bool mask_ok(uint32_t m)           /* contiguous ones from the top, not all zero */
{
    if (m == 0) return false;
    const uint32_t inv = ~m;
    return (inv & (inv + 1u)) == 0;
}
static bool baud_ok(uint32_t b)         /* register value = baud / 100 (115200 does not fit a 16-bit register) */
{
    static const uint32_t ok[] = {12, 24, 48, 96, 192, 384, 576, 1152};
    for (unsigned i = 0; i < sizeof(ok) / sizeof(ok[0]); ++i) if (ok[i] == b) return true;
    return false;
}
unsigned fw_net_validate(const uint16_t *n, bool model_rtu)
{
    unsigned bad = seg_validate(&FW_SEG_NET, n);
    if (bad) return bad;
    if (!mask_ok(fw_w32(n, FW_N_MASK))) return FW_N_MASK + 1u;
    if (!baud_ok(n[FW_N_RTU_BAUD])) return FW_N_RTU_BAUD + 1u;
    if (!fw_mode_write_ok(n[FW_N_COMM_MODE], model_rtu)) return FW_N_COMM_MODE + 1u;
    return 0;
}

bool fw_mode_write_ok(uint16_t requested, bool model_rtu)
{
    if (requested == FW_MODE_TCP) return true;
    return requested == FW_MODE_RTU && model_rtu;       /* TCP model has no RS-485 hardware: refuse (D-059) */
}
uint16_t fw_mode_effective(uint16_t stored, bool model_rtu, bool *fell_back)
{
    const bool ok = fw_mode_write_ok(stored, model_rtu);
    if (fell_back) *fell_back = !ok;
    return ok ? stored : (uint16_t)FW_MODE_TCP;
}

static void seg_defaults(const fw_seg_t *s, uint16_t *w)
{
    for (unsigned i = 0; i < s->n; ++i) {
        const fw_ent_t *e = &s->e[i];
        if (e->words == 1) w[e->off] = (uint16_t)e->def;
        else fw_set32(w, e->off, (uint32_t)(int64_t)e->def);
    }
}

void fw_cfg_defaults(fw_cfg_t *c)
{
    memset(c, 0, sizeof(*c));
    c->version = FW_CFG_VERSION;
    fw_set32(c->leg, 0, 5000u);                 /* servo1axis default Position */
    c->leg[2] = 1u;                              /* direction forward */
    seg_defaults(&FW_SEG_SYS, c->sys);
    seg_defaults(&FW_SEG_NET, c->net);
    for (unsigned a = 0; a < FW_MAX_AXES; ++a) {
        seg_defaults(&FW_SEG_PAR, c->par[a]);
        seg_defaults(&FW_SEG_OP, c->op[a]);
    }
    fw_cfg_seal(c);
}

void fw_cfg_seal(fw_cfg_t *c)
{
    c->version = FW_CFG_VERSION;
    c->crc = fw_crc32(c, offsetof(fw_cfg_t, crc));
}

fw_cfg_check_t fw_cfg_verify(const fw_cfg_t *c, size_t stored_size)
{
    if (stored_size != sizeof(*c)) return FW_CFG_BAD_SIZE;
    if (c->version != FW_CFG_VERSION) return FW_CFG_BAD_VERSION;
    if (c->crc != fw_crc32(c, offsetof(fw_cfg_t, crc))) return FW_CFG_BAD_CRC;
    if (fw_w32(c->leg, 0) < 1u || c->leg[2] > 1u) return FW_CFG_BAD_RANGE;
    if (fw_sys_validate(c->sys)) return FW_CFG_BAD_RANGE;
    /* the stored mode is checked against the hardware at boot (fw_mode_effective), not here */
    if (seg_validate(&FW_SEG_NET, c->net) || !mask_ok(fw_w32(c->net, FW_N_MASK)) || !baud_ok(c->net[FW_N_RTU_BAUD]))
        return FW_CFG_BAD_RANGE;
    for (unsigned a = 0; a < FW_MAX_AXES; ++a)
        if (fw_par_validate(c->par[a]) || fw_op_validate(c->op[a], c->par[a], false)) return FW_CFG_BAD_RANGE;
    return FW_CFG_OK;
}

/* ---- deferred save ---- */
void fw_save_note_write(fw_save_t *s, uint32_t now_ms) { s->dirty = true; s->last_write_ms = now_ms; }
void fw_save_request(fw_save_t *s) { s->save_req = true; }
bool fw_save_due(const fw_save_t *s, uint32_t now_ms, bool auto_save, bool any_running)
{
    if (any_running) return false;
    if (s->save_req) return true;
    return s->dirty && auto_save && (uint32_t)(now_ms - s->last_write_ms) >= FW_AUTO_SAVE_DELAY_MS;
}
void fw_save_done(fw_save_t *s) { s->dirty = false; s->save_req = false; }

/* ---- comm loss ---- */
void fw_commloss_init(fw_commloss_t *c, uint32_t now_ms)
{
    c->last_any_ms = now_ms; c->last_cmd_ms = now_ms; c->cmd_conn = -1; c->cmd_conn_open = false;
}
void fw_commloss_activity(fw_commloss_t *c, int32_t conn, bool is_command, uint32_t now_ms)
{
    c->last_any_ms = now_ms;
    if (is_command) { c->cmd_conn = conn; c->cmd_conn_open = true; c->last_cmd_ms = now_ms; }
    else if (conn == c->cmd_conn) c->last_cmd_ms = now_ms;       /* any request of the commanding connection counts */
}
void fw_commloss_conn_closed(fw_commloss_t *c, int32_t conn)
{
    if (conn == c->cmd_conn) c->cmd_conn_open = false;
}
bool fw_commloss_check(const fw_commloss_t *c, bool enable, uint32_t time_ms, uint16_t scope, bool running, uint32_t now_ms)
{
    if (!enable || !running) return false;
    if (scope == 0) return (uint32_t)(now_ms - c->last_any_ms) > time_ms;      /* no request on ANY connection */
    if (c->cmd_conn < 0) return false;                                          /* nobody ever commanded */
    return !c->cmd_conn_open || (uint32_t)(now_ms - c->last_cmd_ms) > time_ms;  /* the commanding connection */
}

bool fw_pin_conflict(const int *a, size_t na, const int *b, size_t nb)
{
    for (size_t i = 0; i < na; ++i) {
        if (a[i] < 0) continue;
        for (size_t j = i + 1; j < na; ++j) if (a[j] == a[i]) return true;
        for (size_t j = 0; j < nb; ++j) if (b[j] == a[i]) return true;
    }
    return false;
}
