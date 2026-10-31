#include "tfw_logic.h"
#include <string.h>

typedef struct { uint16_t addr; uint8_t words; uint8_t writable; } item_t;

static const item_t k_items[] = {
    {TFW_R_CMD, 1, 1}, {TFW_R_AXIS_MASK, 1, 1}, {TFW_R_FREQ, 2, 1}, {TFW_R_COUNT, 2, 1},
    {TFW_R_ACCEL_MS, 1, 1}, {TFW_R_DECEL_MS, 1, 1}, {TFW_R_REPEAT, 2, 1}, {TFW_R_SEED, 2, 1},
    {TFW_R_LOAD_NVS_MS, 1, 1}, {TFW_R_LOAD_CPU_PCT, 1, 1}, {TFW_R_CFG_UPD_US, 1, 1},
    {TFW_R_CFG_RES_HZ, 2, 1}, {TFW_R_CFG_FLAGS, 1, 1},
    {TFW_R_SEQ_DONE, 2, 0}, {TFW_R_ERR_AX1, 2, 0}, {TFW_R_ERR_AX2, 2, 0}, {TFW_R_OVS_AX1, 2, 0},
    {TFW_R_OVS_AX2, 2, 0}, {TFW_R_UPD_LATE, 2, 0}, {TFW_R_UPD_MAX_US, 2, 0}, {TFW_R_ISR_LAT_AX1, 2, 0},
    {TFW_R_ISR_LAT_AX2, 2, 0}, {TFW_R_NVS_WRITES, 2, 0}, {TFW_R_NVS_MAX_MS, 2, 0},
    {TFW_R_CHIP_REV, 1, 0}, {TFW_R_CPU_MHZ, 1, 0}, {TFW_R_STATUS, 1, 0},
    {TFW_R_LAST_TGT_AX1, 2, 0}, {TFW_R_LAST_CNT_AX1, 2, 0}, {TFW_R_LAST_TGT_AX2, 2, 0}, {TFW_R_LAST_CNT_AX2, 2, 0},
    {TFW_R_MB_CLIENTS, 1, 0}, {TFW_R_MB_LINK_DOWN, 1, 0}, {TFW_R_MB_REQ_COUNT, 2, 0}, {TFW_R_MB_PROC_MAX_US, 2, 0},
    {TFW_R_MB_HIST + 0, 1, 0}, {TFW_R_MB_HIST + 1, 1, 0}, {TFW_R_MB_HIST + 2, 1, 0}, {TFW_R_MB_HIST + 3, 1, 0},
    {TFW_R_MB_HIST + 4, 1, 0}, {TFW_R_MB_HIST + 5, 1, 0}, {TFW_R_MB_HIST + 6, 1, 0}, {TFW_R_MB_HIST + 7, 1, 0},
    {TFW_R_MB_MAX_CLIENTS, 1, 0},
    {TFW_R_ES_PATH_SEL, 1, 1}, {TFW_R_ES_RELEASE, 1, 1}, {TFW_R_ES_STATE, 1, 0}, {TFW_R_ES_TRIP_COUNT, 1, 0},
    {TFW_R_ES_ISR_LAT, 2, 0}, {TFW_R_ES_PULSES_AX1, 2, 0}, {TFW_R_ES_PULSES_AX2, 2, 0},
    {TFW_R_ES_BIND_OK, 1, 0}, {TFW_R_ES_BOOT_LEVEL, 1, 0}, {TFW_R_ES_LAST_ERR, 1, 0},
    {TFW_R_T15_HANG, 1, 1}, {TFW_R_RESET_REASON, 1, 0}, {TFW_R_BOOT_COUNT, 1, 0},
};
#define N_ITEMS (sizeof(k_items) / sizeof(k_items[0]))

/* item containing address a; *word = 0 for the first (low) word */
static const item_t *find_item(uint16_t a, unsigned *word)
{
    for (unsigned i = 0; i < N_ITEMS; ++i) {
        const item_t *it = &k_items[i];
        if (a >= it->addr && a < it->addr + it->words) { *word = a - it->addr; return it; }
    }
    return NULL;
}

void tfw_regwin_init(tfw_regwin_t *w, uint16_t build_flags)
{
    memset(w, 0, sizeof(*w));
    w->axis_mask = 1;
    w->freq = 100000;
    w->count = 1000;
    w->accel_ms = 100;
    w->decel_ms = 100;
    w->repeat = 1;
    w->cfg_upd_us = 1000;
    w->cfg_res_hz = 10000000u;
    w->cfg_flags = build_flags & TFW_CFG_PCNT_IRAM;
    w->es_path_sel = 3;
}

/* returns true if value accepted and stored */
static bool set_value(tfw_regwin_t *w, uint16_t addr, uint32_t v, tfw_wr_effect_t *fx, bool running)
{
    switch (addr) {
    case TFW_R_CMD:
        if (v < 1 || v > 6) return false;
        fx->cmd = (uint16_t)v; return true;
    case TFW_R_AXIS_MASK:  if (v < 1 || v > 3) return false; w->axis_mask = (uint16_t)v; return true;
    case TFW_R_FREQ:       if (v < 100 || v > 1000000u) return false; w->freq = v; return true;
    case TFW_R_COUNT:      if (v < 1) return false; w->count = v; return true;
    case TFW_R_ACCEL_MS:   if (v < 10 || v > 60000) return false; w->accel_ms = (uint16_t)v; return true;
    case TFW_R_DECEL_MS:   if (v < 10 || v > 60000) return false; w->decel_ms = (uint16_t)v; return true;
    case TFW_R_REPEAT:     if (v < 1 || v > 1000000u) return false; w->repeat = v; return true;
    case TFW_R_SEED:       w->seed = v; return true;
    case TFW_R_LOAD_NVS_MS:
        if (v != 0 && (v < 10 || v > 10000)) return false;
        w->load_nvs_ms = (uint16_t)v; fx->load_changed = true; return true;
    case TFW_R_LOAD_CPU_PCT:
        if (v > 90) return false;
        w->load_cpu_pct = (uint16_t)v; fx->load_changed = true; return true;
    case TFW_R_CFG_UPD_US:
        if (running || v < 500 || v > 10000) return false;
        w->cfg_upd_us = (uint16_t)v; fx->cfg_changed = true; return true;
    case TFW_R_CFG_RES_HZ:
        if (running || !tfw_res_valid(v)) return false;
        w->cfg_res_hz = v; fx->cfg_changed = true; return true;
    case TFW_R_CFG_FLAGS:
        if (running || (v & ~0x000Fu)) return false;
        w->cfg_flags = (uint16_t)((w->cfg_flags & TFW_CFG_PCNT_IRAM) | (v & TFW_CFG_WRITABLE_MASK));
        fx->cfg_changed = true; return true;
    case TFW_R_ES_PATH_SEL:
        if (!tfw_es_path_valid((uint8_t)v) || v > 3) return false;
        w->es_path_sel = (uint16_t)v; fx->es_path_set = true; return true;
    case TFW_R_ES_RELEASE:
        if (v != 1) return false;
        fx->es_release = true; return true;
    case TFW_R_T15_HANG:   /* only the two magic values; the build option gate is applied by the firmware */
        if (v != TFW_T15_MAGIC_TASK && v != TFW_T15_MAGIC_IRQOFF) return false;
        fx->t15_hang = (uint16_t)v; return true;
    default: return false;
    }
}

int tfw_regwin_write(tfw_regwin_t *w, uint16_t addr, uint16_t count, const uint16_t *v, tfw_wr_effect_t *fx)
{
    if (addr < TFW_WIN_BASE || count == 0 || (uint32_t)addr + count - 1u > TFW_WIN_END) return 2;
    /* pass 1: validate coverage */
    for (uint32_t a = addr; a < (uint32_t)addr + count;) {
        unsigned word;
        const item_t *it = find_item((uint16_t)a, &word);
        if (!it || !it->writable) return 2;
        if (it->words == 2) {
            if (word != 0 || a + 1 >= (uint32_t)addr + count) return 3;   /* half of a 32-bit item */
        }
        a += it->words;
    }
    /* pass 2: apply */
    const bool running = (w->status == TFW_ST_RUNNING);
    bool bad = false;
    for (uint32_t a = addr; a < (uint32_t)addr + count;) {
        unsigned word;
        const item_t *it = find_item((uint16_t)a, &word);
        uint32_t val = v[a - addr];
        if (it->words == 2) val |= (uint32_t)v[a - addr + 1] << 16;
        if (!set_value(w, (uint16_t)a, val, fx, running)) bad = true;
        a += it->words;
    }
    if (bad && !running) w->status = TFW_ST_BAD_VALUE;
    return 0;
}

static uint32_t get_value(const tfw_regwin_t *w, const tfw_results_t *r, uint16_t a)
{
    switch (a) {
    case TFW_R_CMD: return 0;
    case TFW_R_AXIS_MASK: return w->axis_mask;
    case TFW_R_FREQ: return w->freq;
    case TFW_R_COUNT: return w->count;
    case TFW_R_ACCEL_MS: return w->accel_ms;
    case TFW_R_DECEL_MS: return w->decel_ms;
    case TFW_R_REPEAT: return w->repeat;
    case TFW_R_SEED: return w->seed;
    case TFW_R_LOAD_NVS_MS: return w->load_nvs_ms;
    case TFW_R_LOAD_CPU_PCT: return w->load_cpu_pct;
    case TFW_R_CFG_UPD_US: return w->cfg_upd_us;
    case TFW_R_CFG_RES_HZ: return w->cfg_res_hz;
    case TFW_R_CFG_FLAGS: return w->cfg_flags;
    case TFW_R_SEQ_DONE: return r->seq_done;
    case TFW_R_ERR_AX1: return r->err[0];
    case TFW_R_ERR_AX2: return r->err[1];
    case TFW_R_OVS_AX1: return r->overshoot[0];
    case TFW_R_OVS_AX2: return r->overshoot[1];
    case TFW_R_UPD_LATE: return r->upd_late;
    case TFW_R_UPD_MAX_US: return r->upd_max_us;
    case TFW_R_ISR_LAT_AX1: return r->isr_lat_ns[0];
    case TFW_R_ISR_LAT_AX2: return r->isr_lat_ns[1];
    case TFW_R_NVS_WRITES: return r->nvs_writes;
    case TFW_R_NVS_MAX_MS: return r->nvs_max_ms;
    case TFW_R_CHIP_REV: return r->chip_rev;
    case TFW_R_CPU_MHZ: return r->cpu_mhz;
    case TFW_R_STATUS: return w->status;
    case TFW_R_LAST_TGT_AX1: return r->last_target[0];
    case TFW_R_LAST_CNT_AX1: return r->last_pcnt[0];
    case TFW_R_LAST_TGT_AX2: return r->last_target[1];
    case TFW_R_LAST_CNT_AX2: return r->last_pcnt[1];
    case TFW_R_MB_CLIENTS: return r->mb_clients;
    case TFW_R_MB_LINK_DOWN: return r->mb_link_down;
    case TFW_R_MB_MAX_CLIENTS: return r->mb_max_clients;
    case TFW_R_MB_REQ_COUNT: return r->mb_req_count;
    case TFW_R_MB_PROC_MAX_US: return r->mb_proc_max_us;
    case TFW_R_ES_PATH_SEL: return w->es_path_sel;
    case TFW_R_ES_RELEASE: return 0;
    case TFW_R_ES_STATE: return r->es_state;
    case TFW_R_ES_TRIP_COUNT: return r->es_trip_count;
    case TFW_R_ES_ISR_LAT: return r->es_isr_lat_ns;
    case TFW_R_ES_PULSES_AX1: return r->es_pulses_after[0];
    case TFW_R_ES_PULSES_AX2: return r->es_pulses_after[1];
    case TFW_R_ES_BIND_OK: return r->es_bind_ok;
    case TFW_R_ES_BOOT_LEVEL: return r->es_boot_level;
    case TFW_R_ES_LAST_ERR: return r->es_last_err;
    case TFW_R_T15_HANG: return 0;
    case TFW_R_RESET_REASON: return r->reset_reason;
    case TFW_R_BOOT_COUNT: return r->boot_count;
    default:
        if (a >= TFW_R_MB_HIST && a < TFW_R_MB_HIST + 8) return r->mb_hist[a - TFW_R_MB_HIST];
        return 0;
    }
}

int tfw_regwin_read(const tfw_regwin_t *w, const tfw_results_t *r, uint16_t addr, uint16_t count, uint16_t *out)
{
    if (addr < TFW_WIN_BASE || count == 0 || (uint32_t)addr + count - 1u > TFW_WIN_END) return 2;
    for (uint32_t a = addr; a < (uint32_t)addr + count; ++a) {
        unsigned word;
        const item_t *it = find_item((uint16_t)a, &word);
        if (!it) { out[a - addr] = 0; continue; }
        uint32_t v = get_value(w, r, it->addr);
        out[a - addr] = word ? (uint16_t)(v >> 16) : (uint16_t)v;
    }
    return 0;
}

uint16_t tfw_cmd_check(const tfw_regwin_t *w, uint16_t cmd, bool running, bool latched, bool ax2_ok)
{
    switch (cmd) {
    case TFW_CMD_STOP_ALL: case TFW_CMD_SET_LOAD: return 0;
    case TFW_CMD_CLEAR_STATS: return running ? TFW_ST_REJECT_BUSY : 0;
    case TFW_CMD_RUN_FIXED: case TFW_CMD_RUN_MOVE: case TFW_CMD_RUN_SEQ:
        if (running || latched) return TFW_ST_REJECT_BUSY;
        if (w->axis_mask < 1 || w->axis_mask > 3) return TFW_ST_BAD_VALUE;
        if ((w->axis_mask & 2) && !ax2_ok) return TFW_ST_NO_RESOURCE;
        return 0;
    default: return TFW_ST_BAD_VALUE;
    }
}
