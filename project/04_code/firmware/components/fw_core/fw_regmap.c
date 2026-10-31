#include "fw_regmap.h"
#include <string.h>

/* ------------------------------------------------------------------ item tables */
enum {
    I_NONE = 0,
    /* system status 0x0020.. */
    I_MODEL, I_AXISCNT, I_FWVER, I_REGVER, I_SYSST, I_ESTOP, I_CLIENTS, I_UPTIME, I_HB, I_SYSIN, I_RSTR, I_BOOT,
    I_MODEA, I_ALMASK, I_DIRAW, I_DOST,
    /* 0x0100.. */
    I_SYSCMD, I_SYSRES, I_UNLOCK, I_USERDO,
    /* alarm history */
    I_HISTCNT, I_HISTLAT,
    /* network */
    I_MAC0, I_MAC1, I_MAC2, I_NETAPPLY,
    /* axis data / diag / command */
    A_STATE, A_STATUS, A_CMDPOS, A_CMDSPD, A_TGTACT, A_ALARM, A_STOPCAUSE, A_INPUTS, A_CMDRES, A_MOVECNT, A_PULSES,
    A_CMD, A_ALARM2, A_ALCOUNT, A_UNDERRUN, A_SEGLAT, A_HOMELATCH
};
typedef struct { uint16_t off; uint8_t words; uint8_t id; } itm_t;

static const itm_t SYSI[] = {
    {0x0020, 1, I_MODEL}, {0x0021, 1, I_AXISCNT}, {0x0022, 2, I_FWVER}, {0x0024, 1, I_REGVER}, {0x0025, 1, I_SYSST},
    {0x0026, 1, I_ESTOP}, {0x0027, 1, I_CLIENTS}, {0x0028, 2, I_UPTIME}, {0x002A, 1, I_HB}, {0x002B, 1, I_SYSIN},
    {0x002C, 1, I_RSTR}, {0x002D, 1, I_BOOT}, {0x002E, 1, I_MODEA}, {0x0030, 1, I_ALMASK}, {0x0031, 1, I_DIRAW},
    {0x0032, 1, I_DOST},
    {0x0100, 1, I_SYSCMD}, {0x0101, 1, I_SYSRES}, {0x0102, 1, I_UNLOCK}, {0x0120, 1, I_USERDO},
    {0x3000, 1, I_HISTCNT}, {0x3001, 1, I_HISTLAT},
    {0x4007, 1, I_MAC0}, {0x4008, 1, I_MAC1}, {0x4009, 1, I_MAC2}, {0x400F, 1, I_NETAPPLY},
};
static const itm_t AXI[] = {   /* offsets relative to B */
    {0x000, 1, A_STATE}, {0x001, 1, A_STATUS}, {0x002, 2, A_CMDPOS}, {0x004, 2, A_CMDSPD}, {0x006, 2, A_TGTACT},
    {0x008, 1, A_ALARM}, {0x009, 1, A_STOPCAUSE}, {0x00A, 1, A_INPUTS}, {0x00B, 1, A_CMDRES}, {0x00C, 2, A_MOVECNT},
    {0x00E, 2, A_PULSES}, {0x040, 1, A_CMD},
    {0x200, 1, A_ALARM2}, {0x201, 1, A_ALCOUNT}, {0x202, 2, A_UNDERRUN}, {0x204, 1, A_SEGLAT}, {0x205, 2, A_HOMELATCH},
};
#define NSYS ((unsigned)(sizeof(SYSI) / sizeof(SYSI[0])))
#define NAXI ((unsigned)(sizeof(AXI) / sizeof(AXI[0])))

static const itm_t *find_itm(const itm_t *t, unsigned n, unsigned addr)
{
    for (unsigned i = 0; i < n; ++i)
        if (addr >= t[i].off && addr < (unsigned)t[i].off + t[i].words) return &t[i];
    return NULL;
}

/* where does an address live */
typedef enum { R_UNDEF = 0, R_LEGACY, R_SYSBLK, R_CMDBLK, R_AXIS, R_HIST, R_NET } region_t;
typedef struct { region_t r; int axis; unsigned off; } loc_t;

static loc_t locate(const fw_dev_t *d, unsigned a)
{
    loc_t l = {R_UNDEF, -1, a};
    if (a <= 0x001F) { l.r = R_LEGACY; return l; }
    if (a >= 0x0020 && a <= 0x00FF) { l.r = R_SYSBLK; return l; }
    if (a >= 0x0100 && a <= 0x01FF) { l.r = R_CMDBLK; return l; }
    if (a >= 0x3000 && a <= 0x30FF) { l.r = R_HIST; return l; }
    if (a >= 0x4000 && a <= 0x40FF) { l.r = R_NET; l.off = a - 0x4000; return l; }
    if (a >= 0x1000 && a <= 0x2FFF) {
        const int ax = (int)(a >> 12) - 1;
        if (ax >= (int)d->axis_count) return l;          /* 1-axis model: axis 2 area -> exception 02 */
        const unsigned off = a & 0xFFF;
        if (off < 0x80 || (off >= 0x100 && off < 0x200) || (off >= 0x200 && off < 0x240)) {
            l.r = R_AXIS; l.axis = ax; l.off = off;
        }
    }
    return l;
}

bool fw_dev_unlocked(const fw_dev_t *d)
{
    return d->unlock_active && (int32_t)(d->unlock_deadline_ms - d->now_ms) > 0;
}

void fw_hist_push(fw_dev_t *d, uint16_t code, uint16_t axis)
{
    memmove(&d->hist[1], &d->hist[0], sizeof(d->hist[0]) * (FW_HIST_N - 1));
    d->hist[0].code = code; d->hist[0].axis = axis; d->hist[0].uptime_s = d->uptime_s;
    if (d->hist_count < FW_HIST_N) d->hist_count++;
}

void fw_dev_init(fw_dev_t *d, uint8_t axis_count, bool model_rtu)
{
    memset(d, 0, sizeof(*d));
    d->axis_count = axis_count > FW_MAX_AXES ? FW_MAX_AXES : (axis_count ? axis_count : 1);
    d->model_rtu = model_rtu;
    fw_cfg_defaults(&d->cfg);
}

/* ------------------------------------------------------------------ reads */
static uint32_t item_val(const fw_dev_t *d, int ax, uint8_t id)
{
    const fw_axis_tele_t *t = &d->ax[ax < 0 ? 0 : ax];
    switch (id) {
    case I_MODEL: return d->axis_count;
    case I_AXISCNT: return d->axis_count;
    case I_FWVER: return d->fw_version;
    case I_REGVER: return FW_REGMAP_VERSION;
    case I_SYSST: return d->sys_status;
    case I_ESTOP: return d->estop_state;
    case I_CLIENTS: return d->tcp_clients;
    case I_UPTIME: return d->uptime_s;
    case I_HB: return d->heartbeat;
    case I_SYSIN: return d->sys_inputs;
    case I_RSTR: return d->reset_reason;
    case I_BOOT: return d->boot_status;
    case I_MODEA: return d->comm_mode_active;
    case I_ALMASK: return d->alarm_axis_mask;
    case I_DIRAW: return d->di_raw;
    case I_DOST: return d->do_state;
    case I_USERDO: return d->user_do;
    case I_SYSRES: return d->sys_cmd_result;
    case I_HISTCNT: return d->hist_count;
    case I_HISTLAT: return 0;                           /* index of the newest entry: entry 0 is always the newest */
    case I_MAC0: return ((uint32_t)d->mac[0] << 8) | d->mac[1];
    case I_MAC1: return ((uint32_t)d->mac[2] << 8) | d->mac[3];
    case I_MAC2: return ((uint32_t)d->mac[4] << 8) | d->mac[5];
    case A_STATE: return t->state;
    case A_STATUS: return t->status;
    case A_CMDPOS: return (uint32_t)t->cmd_position;
    case A_CMDSPD: return (uint32_t)t->cmd_speed;
    case A_TGTACT: return (uint32_t)t->target_position_act;
    case A_ALARM: case A_ALARM2: return t->alarm_code;
    case A_STOPCAUSE: return t->last_stop_cause;
    case A_INPUTS: return t->axis_inputs;
    case A_CMDRES: return t->cmd_result;
    case A_MOVECNT: return t->move_count;
    case A_PULSES: return t->pulses_done;
    case A_ALCOUNT: return t->alarm_count;
    default: return 0;                                  /* write-only commands read 0, unimplemented diag = 0 */
    }
}

static uint16_t legacy_status(const fw_dev_t *d)
{
    const fw_axis_tele_t *t = &d->ax[0];
    uint16_t s = 0;
    if (d->link) s |= 1u << 0;
    if (d->tcp_clients) s |= 1u << 1;
    if (t->servo_manual) s |= 1u << 2;
    if (t->moving) s |= 1u << 3;
    if (t->leg_result == 1) s |= 1u << 4;
    if (t->leg_fault) s |= 1u << 5;
    return s;
}

static int read_legacy(const fw_dev_t *d, unsigned a, uint16_t *out)
{
    const fw_axis_tele_t *t = &d->ax[0];
    const uint16_t *op = d->cfg.op[0];
    switch (a) {
    case 0: case 1: *out = d->cfg.leg[a]; return 0;
    case 2: case 3: *out = op[FW_O_SPEED + (a - 2)]; return 0;
    case 6: *out = (uint16_t)t->pulses_done; return 0;
    case 7: *out = (uint16_t)(t->pulses_done >> 16); return 0;
    case 10: *out = legacy_status(d); return 0;
    case 13: *out = t->leg_result; return 0;
    case 14: *out = t->leg_fault; return 0;
    case 15: *out = d->tcp_clients; return 0;
    case 16: *out = d->link ? 1 : 0; return 0;
    case 18: case 19: {
        const uint32_t sp = fw_start_speed(fw_w32(d->cfg.par[0], FW_P_START_SPEED), fw_w32(op, FW_O_SPEED));
        *out = (uint16_t)(a == 18 ? sp : sp >> 16);
        return 0;
    }
    case 20: *out = op[FW_O_ACCEL]; return 0;
    case 22: *out = op[FW_O_DECEL]; return 0;
    default: *out = 0; return 0;       /* 4,5 (unused), 21,23 (high words), reserved: read 0 */
    }
}

static int read_seg(const fw_seg_t *s, const uint16_t *w, unsigned off, uint16_t *out)
{
    *out = (off < s->nwords) ? w[off] : 0;
    return 0;
}

int fw_reg_read(const fw_dev_t *d, uint16_t addr, uint16_t *out)
{
    const loc_t l = locate(d, addr);
    const itm_t *it;
    switch (l.r) {
    case R_LEGACY: return read_legacy(d, addr, out);
    case R_SYSBLK: case R_CMDBLK:
        if (l.r == R_CMDBLK && addr >= 0x0110 && addr < 0x0110 + FW_SYS_WORDS) { *out = d->cfg.sys[addr - 0x0110]; return 0; }
        it = find_itm(SYSI, NSYS, addr);
        *out = it ? (uint16_t)(item_val(d, -1, it->id) >> (16 * (addr - it->off))) : 0;
        return 0;
    case R_HIST:
        if (addr >= 0x3010 && addr < 0x3010 + 4 * FW_HIST_N) {
            const unsigned i = (addr - 0x3010) / 4, w = (addr - 0x3010) % 4;
            if (i >= d->hist_count) { *out = 0; return 0; }
            const fw_hist_t *h = &d->hist[i];
            *out = w == 0 ? h->code : w == 1 ? h->axis : w == 2 ? (uint16_t)h->uptime_s : (uint16_t)(h->uptime_s >> 16);
            return 0;
        }
        it = find_itm(SYSI, NSYS, addr);
        *out = it ? (uint16_t)item_val(d, -1, it->id) : 0;
        return 0;
    case R_NET:
        if (addr == 0x4000 + FW_N_COMM_MODE + 1) { *out = 0; return 0; }
        it = find_itm(SYSI, NSYS, addr);
        if (it) { *out = (uint16_t)item_val(d, -1, it->id); return 0; }
        return read_seg(&FW_SEG_NET, d->cfg.net, l.off, out);
    case R_AXIS:
        if (l.off >= 0x40 && l.off < 0x80) {
            if (l.off == 0x40) { *out = 0; return 0; }
            return read_seg(&FW_SEG_OP, d->cfg.op[l.axis], l.off - 0x40, out);
        }
        if (l.off >= 0x100 && l.off < 0x200) return read_seg(&FW_SEG_PAR, d->cfg.par[l.axis], l.off - 0x100, out);
        it = find_itm(AXI, NAXI, l.off);
        *out = it ? (uint16_t)(item_val(d, l.axis, it->id) >> (16 * (l.off - it->off))) : 0;
        return 0;
    default: return 2;
    }
}

/* ------------------------------------------------------------------ writes */
typedef enum { W_UNDEF = 0, W_LEG, W_SEG, W_AXCMD, W_SYSCMD, W_UNLOCK, W_USERDO, W_NETAPPLY } wkind_t;
typedef struct {
    wkind_t k;
    int axis;
    const fw_seg_t *seg;          /* W_SEG */
    int segid;                    /* 0 par, 1 op, 2 sys, 3 net */
    unsigned segoff;
    unsigned istart, iwords;      /* item boundaries (absolute address) */
} wt_t;

static wt_t wclassify(const fw_dev_t *d, unsigned a)
{
    wt_t t = {W_UNDEF, -1, NULL, 0, 0, a, 1};
    const loc_t l = locate(d, a);
    const itm_t *it;
    const fw_ent_t *e;
    switch (l.r) {
    case R_LEGACY:
        if (a <= 3 || (a >= 20 && a <= 23)) { t.k = W_LEG; }      /* each word on its own (legacy half writes are legal) */
        return t;
    case R_CMDBLK:
        if (a >= 0x0110 && a < 0x0110 + FW_SYS_WORDS) {
            e = fw_seg_find(&FW_SEG_SYS, a - 0x0110);
            if (e) { t.k = W_SEG; t.seg = &FW_SEG_SYS; t.segid = 2; t.segoff = a - 0x0110; t.istart = 0x0110u + e->off; t.iwords = e->words; }
            return t;
        }
        it = find_itm(SYSI, NSYS, a);
        if (it) {
            t.istart = it->off; t.iwords = it->words;
            t.k = it->id == I_SYSCMD ? W_SYSCMD : it->id == I_UNLOCK ? W_UNLOCK : it->id == I_USERDO ? W_USERDO : W_UNDEF;
        }
        return t;
    case R_NET:
        it = find_itm(SYSI, NSYS, a);
        if (it) { if (it->id == I_NETAPPLY) { t.k = W_NETAPPLY; } return t; }       /* MAC: read-only -> 02 */
        e = fw_seg_find(&FW_SEG_NET, l.off);
        if (e) { t.k = W_SEG; t.seg = &FW_SEG_NET; t.segid = 3; t.segoff = l.off; t.istart = 0x4000u + e->off; t.iwords = e->words; }
        return t;
    case R_AXIS:
        t.axis = l.axis;
        if (l.off >= 0x40 && l.off < 0x80) {
            if (l.off == 0x40) { t.k = W_AXCMD; return t; }
            e = fw_seg_find(&FW_SEG_OP, l.off - 0x40);
            if (e) { t.k = W_SEG; t.seg = &FW_SEG_OP; t.segid = 1; t.segoff = l.off - 0x40;
                     t.istart = (a - l.off) + 0x40u + e->off; t.iwords = e->words; }
        } else if (l.off >= 0x100 && l.off < 0x200) {
            e = fw_seg_find(&FW_SEG_PAR, l.off - 0x100);
            if (e) { t.k = W_SEG; t.seg = &FW_SEG_PAR; t.segid = 0; t.segoff = l.off - 0x100;
                     t.istart = (a - l.off) + 0x100u + e->off; t.iwords = e->words; }
        }
        return t;
    default: return t;
    }
}

static bool axis_cmd_supported(uint16_t c)
{
    return c == FW_CMD_START_ABS || c == FW_CMD_START_REL || c == FW_CMD_STOP || c == FW_CMD_ALARM_RESET ||
           c == FW_CMD_SERVO_ON || c == FW_CMD_SERVO_OFF || c == FW_CMD_SET_POSITION || c == FW_CMD_QUICK_STOP;
}

/* returns 0 or an exception code; commits atomically */
static int write_regs(fw_dev_t *d, unsigned addr, unsigned count, const uint16_t *v)
{
    fw_cfg_t cand = d->cfg;
    bool t_par[FW_MAX_AXES] = {0}, t_op[FW_MAX_AXES] = {0}, t_opx[FW_MAX_AXES] = {0};   /* t_opx: via axis area */
    bool t_sys = false, t_net = false, t_leg = false;
    uint16_t axcmd[FW_MAX_AXES]; bool axcmd_v[FW_MAX_AXES] = {0};
    uint16_t syscmd = 0; bool syscmd_v = false;
    bool unlock_v = false, userdo_v = false, netapply_v = false; uint16_t userdo = 0, unlockval = 0;

    for (unsigned a = addr; a < addr + count;) {
        const wt_t t = wclassify(d, a);
        const uint16_t w0 = v[a - addr];
        if (t.k == W_UNDEF) return 2;
        if (t.k == W_LEG) {
            if (a <= 1) { cand.leg[a] = w0; t_leg = true; }
            else if (a <= 3) { cand.op[0][FW_O_SPEED + (a - 2)] = w0; t_op[0] = true; }
            else if (a == 20 || a == 22) { cand.op[0][a == 20 ? FW_O_ACCEL : FW_O_DECEL] = w0; t_op[0] = true; }
            else if (w0 != 0) return 3;                       /* 21, 23: high word of a 16-bit value must be 0 */
            a++;
            continue;
        }
        if (a + t.iwords > addr + count || a != t.istart) return 3;      /* 32-bit item written by halves */
        switch (t.k) {
        case W_SEG: {
            uint16_t *w = t.segid == 0 ? cand.par[t.axis] : t.segid == 1 ? cand.op[t.axis] :
                          t.segid == 2 ? cand.sys : cand.net;
            memcpy(&w[t.segoff], &v[a - addr], t.iwords * sizeof(uint16_t));
            if (t.segid == 0) t_par[t.axis] = true;
            else if (t.segid == 1) { t_op[t.axis] = true; t_opx[t.axis] = true; }
            else if (t.segid == 2) t_sys = true; else t_net = true;
            break;
        }
        case W_AXCMD:
            if (w0 != 0 && !axis_cmd_supported(w0)) {
                if (w0 <= FW_CMD_QUICK_STOP) d->ax[t.axis].cmd_result = FW_RES_RANGE;   /* HOME / JOG: not in v1 */
                return 3;
            }
            axcmd[t.axis] = w0; axcmd_v[t.axis] = w0 != 0;
            break;
        case W_SYSCMD:
            if (w0 != 0 && (w0 > FW_SYS_STOP_ALL)) return 3;
            syscmd = w0; syscmd_v = w0 != 0;
            break;
        case W_UNLOCK: if (w0 != FW_UNLOCK_KEY) return 3; unlock_v = true; unlockval = w0; break;
        case W_USERDO: if (w0 & ~3u) return 3; userdo = w0; userdo_v = true; break;
        case W_NETAPPLY: if (w0 != 1) return 3; netapply_v = true; break;
        default: return 2;
        }
        a += t.iwords;
    }

    /* validation of the candidate image */
    if (t_leg && fw_w32(cand.leg, 0) < 1u) return 3;
    for (unsigned a = 0; a < d->axis_count; ++a) {
        if (t_par[a] && fw_par_validate(cand.par[a])) return 3;
        if (t_par[a] || t_op[a]) {
            if (fw_op_validate(cand.op[a], cand.par[a], t_opx[a])) return 3;
        }
    }
    if (t_sys && fw_sys_validate(cand.sys)) return 3;
    if (t_net && fw_net_validate(cand.net, d->model_rtu)) return 3;

    /* commit */
    cand.crc = d->cfg.crc;
    const bool changed = memcmp(&cand, &d->cfg, sizeof(cand)) != 0;
    if (changed) {
        d->cfg = cand;
        if (d->ops.cfg_changed) d->ops.cfg_changed(d->ops.ctx);
    }
    if (unlock_v) { d->unlock_active = true; d->unlock_deadline_ms = d->now_ms + FW_UNLOCK_MS; (void)unlockval; }
    if (userdo_v) { d->user_do = userdo; if (d->ops.user_do) d->ops.user_do(d->ops.ctx, userdo); }
    for (unsigned a = 0; a < d->axis_count; ++a)
        if (axcmd_v[a] && d->ops.axis_cmd) d->ops.axis_cmd(d->ops.ctx, (uint8_t)a, axcmd[a]);
    if (syscmd_v && d->ops.sys_cmd) d->sys_cmd_result = d->ops.sys_cmd(d->ops.ctx, syscmd);
    if (netapply_v && d->ops.sys_cmd) d->sys_cmd_result = d->ops.sys_cmd(d->ops.ctx, FW_SYS_NET_APPLY);
    return 0;
}

/* ------------------------------------------------------------------ coils */
static bool coil_defined(const fw_dev_t *d, unsigned a)
{
    return a <= 7 || (a >= 0x10 && a <= 0x13) || (d->axis_count >= 2 && a >= 0x20 && a <= 0x23);
}

static bool coil_read(const fw_dev_t *d, unsigned a)
{
    if (a <= 7) {
        switch (a) {
        case 1: return d->ax[0].servo_manual;
        case 3: return (d->cfg.leg[2] & 1u) != 0;
        case 0: case 2: return false;
        default: return (d->coils_hi >> a) & 1u;
        }
    }
    const unsigned ax = (a >> 4) - 1, c = a & 0xF;
    return c == 3 ? d->ax[ax].servo_manual : false;
}

/* returns 0 / exception */
static int coils_write(fw_dev_t *d, unsigned addr, unsigned count, const bool *val)
{
    for (unsigned i = 0; i < count; ++i) {
        const unsigned a = addr + i;
        if (!coil_defined(d, a)) return 2;
        if (a >= 0x10 && (a & 0xF) <= 1 && val[i]) return 2;           /* JOG+/JOG- coils: not in v1 */
    }
    bool dir_changed = false;
    for (unsigned i = 0; i < count; ++i) {
        const unsigned a = addr + i;
        const bool on = val[i];
        if (a <= 7) {
            switch (a) {
            case 0: if (on && d->ops.legacy_start) d->ops.legacy_start(d->ops.ctx); break;
            case 1: if (d->ops.axis_cmd) d->ops.axis_cmd(d->ops.ctx, 0, on ? FW_CMD_SERVO_ON : FW_CMD_SERVO_OFF); break;
            case 2: if (on) { if (d->ops.axis_cmd) d->ops.axis_cmd(d->ops.ctx, 0, FW_CMD_QUICK_STOP);
                              if (d->ops.legacy_reset) d->ops.legacy_reset(d->ops.ctx); } break;
            case 3: {
                const uint16_t nv = on ? 1u : 0u;
                if ((d->cfg.leg[2] & 1u) != nv) { d->cfg.leg[2] = nv; dir_changed = true; }
                break;
            }
            default:
                d->coils_hi = (uint16_t)(on ? (d->coils_hi | (1u << a)) : (d->coils_hi & ~(1u << a)));   /* accepted, no function */
            }
        } else {
            const unsigned ax = (a >> 4) - 1, c = a & 0xF;
            if (c == 2 && on && d->ops.axis_cmd) d->ops.axis_cmd(d->ops.ctx, (uint8_t)ax, FW_CMD_STOP);
            else if (c == 3 && d->ops.axis_cmd) d->ops.axis_cmd(d->ops.ctx, (uint8_t)ax, on ? FW_CMD_SERVO_ON : FW_CMD_SERVO_OFF);
        }
    }
    if (dir_changed && d->ops.cfg_changed) d->ops.cfg_changed(d->ops.ctx);
    return 0;
}

/* ------------------------------------------------------------------ PDU */
static uint16_t be16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static size_t exc(uint8_t fc, uint8_t code, uint8_t *r) { r[0] = (uint8_t)(fc | 0x80u); r[1] = code; return 2; }

bool fw_mb_is_command(const uint8_t *req, size_t len)
{
    return len >= 1 && (req[0] == 5 || req[0] == 6 || req[0] == 15 || req[0] == 16);
}

size_t fw_mb_process(fw_dev_t *d, const uint8_t *req, size_t len, uint8_t *resp)
{
    if (!len) return 0;
    const uint8_t fc = req[0];
    if (fc == 1 || fc == 2) {
        if (len != 5) return exc(fc, 3, resp);
        const unsigned a = be16(req + 1), n = be16(req + 3);
        if (!n || n > 64) return exc(fc, 3, resp);
        const unsigned nb = (n + 7u) / 8u;
        uint32_t di = 0;
        if (fc == 2) {
            di = (d->ax[0].moving ? 1u : 0u) | (d->link ? 2u : 0u) | (d->tcp_clients ? 4u : 0u) |
                 (d->ax[0].leg_result == 1 ? 8u : 0u) | (d->ax[0].leg_fault ? 16u : 0u);
            if (a + n > 8) return exc(fc, 2, resp);
        } else {
            for (unsigned i = 0; i < n; ++i) if (!coil_defined(d, a + i)) return exc(fc, 2, resp);
        }
        resp[0] = fc; resp[1] = (uint8_t)nb; memset(resp + 2, 0, nb);
        for (unsigned i = 0; i < n; ++i) {
            const bool on = fc == 2 ? ((di >> (a + i)) & 1u) != 0 : coil_read(d, a + i);
            if (on) resp[2 + i / 8] |= (uint8_t)(1u << (i % 8));
        }
        return 2 + nb;
    }
    if (fc == 3 || fc == 4) {
        if (len != 5) return exc(fc, 3, resp);
        const unsigned a = be16(req + 1), n = be16(req + 3);
        if (!n || n > 125) return exc(fc, 3, resp);
        if (a + n > 0x10000) return exc(fc, 2, resp);
        resp[0] = fc; resp[1] = (uint8_t)(n * 2);
        for (unsigned i = 0; i < n; ++i) {
            uint16_t w = 0;
            if (fw_reg_read(d, (uint16_t)(a + i), &w) != 0) return exc(fc, 2, resp);
            put16(resp + 2 + 2 * i, w);
        }
        return 2 + 2 * n;
    }
    if (fc == 5) {
        if (len != 5) return exc(fc, 3, resp);
        const unsigned a = be16(req + 1), val = be16(req + 3);
        if (val != 0x0000 && val != 0xFF00) return exc(fc, 3, resp);
        const bool on = val == 0xFF00;
        const int e = coils_write(d, a, 1, &on);
        if (e) return exc(fc, (uint8_t)e, resp);
        memcpy(resp, req, 5);
        return 5;
    }
    if (fc == 15) {
        if (len < 6) return exc(fc, 3, resp);
        const unsigned a = be16(req + 1), n = be16(req + 3), bc = req[5];
        if (!n || n > 64 || bc != (n + 7u) / 8u || len != 6u + bc) return exc(fc, 3, resp);
        bool val[64];
        for (unsigned i = 0; i < n; ++i) val[i] = (req[6 + i / 8] >> (i % 8)) & 1u;
        const int e = coils_write(d, a, n, val);
        if (e) return exc(fc, (uint8_t)e, resp);
        resp[0] = fc; put16(resp + 1, (uint16_t)a); put16(resp + 3, (uint16_t)n);
        return 5;
    }
    if (fc == 6) {
        if (len != 5) return exc(fc, 3, resp);
        const uint16_t a = be16(req + 1), v = be16(req + 3);
        const int e = write_regs(d, a, 1, &v);
        if (e) return exc(fc, (uint8_t)e, resp);
        memcpy(resp, req, 5);
        return 5;
    }
    if (fc == 16) {
        if (len < 6) return exc(fc, 3, resp);
        const unsigned a = be16(req + 1), n = be16(req + 3), bc = req[5];
        if (!n || n > 123 || bc != n * 2 || len != 6u + bc) return exc(fc, 3, resp);
        if (a + n > 0x10000) return exc(fc, 2, resp);
        uint16_t v[123];
        for (unsigned i = 0; i < n; ++i) v[i] = be16(req + 6 + 2 * i);
        const int e = write_regs(d, a, n, v);
        if (e) return exc(fc, (uint8_t)e, resp);
        resp[0] = fc; put16(resp + 1, (uint16_t)a); put16(resp + 3, (uint16_t)n);
        return 5;
    }
    return exc(fc, 1, resp);
}
