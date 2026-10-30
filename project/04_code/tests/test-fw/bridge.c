/* Test bridge: exposes the firmware Modbus PDU engine (tfw_logic) to Python via ctypes. */
#include <string.h>
#include "tfw_logic.h"
static tfw_legacy_t L; static tfw_regwin_t W; static tfw_results_t RES; static tfw_mb_ctx_t C;
static void get_res(void *u, tfw_results_t *o) { (void)u; *o = RES; }
void bridge_reset(void) { tfw_legacy_init(&L); tfw_regwin_init(&W, 0); memset(&RES, 0, sizeof RES); memset(&C, 0, sizeof C);
    C.legacy = &L; C.win = &W; C.get_results = get_res; RES.chip_rev = 301; RES.cpu_mhz = 400; }
void bridge_set_seq_done(unsigned v) { RES.seq_done = v; }
unsigned bridge_take_cmd(void) { unsigned c = C.fx.cmd; C.fx.cmd = 0; return c; }
unsigned bridge_status(void) { return W.status; }
unsigned bridge_process(const unsigned char *req, unsigned len, unsigned char *resp) { return (unsigned)tfw_mb_process(&C, req, len, resp); }

/* ---- C-02: record the targets the REAL tfw_seq_run hands to the hardware (fake machine, completes at once) ---- */
static unsigned long long G_TGT[2];
static int f_move(void *c, uint8_t ax, uint32_t sp, uint32_t cnt, uint32_t a, uint32_t d) { (void)c; (void)sp; (void)a; (void)d; G_TGT[ax] += cnt; return 0; }
static int f_fixed(void *c, uint8_t ax, uint32_t f, uint32_t cnt) { (void)c; (void)f; G_TGT[ax] += cnt; return 0; }
static bool f_busy(void *c, uint8_t ax) { (void)c; (void)ax; return false; }
static uint32_t f_count(void *c, uint8_t ax) { (void)c; return (uint32_t)G_TGT[ax]; }   /* unused by the sums below */
static void f_stop(void *c, uint8_t ax) { (void)c; (void)ax; }
static void f_delay(void *c, uint32_t ms) { (void)c; (void)ms; }
static uint32_t f_now(void *c) { (void)c; return 0; }
static bool f_abort(void *c) { (void)c; return false; }
void bridge_seq_targets(unsigned cmd, unsigned mask, unsigned freq, unsigned count, unsigned seed, unsigned repeat, unsigned long long *out)
{
    tfw_regwin_t w; volatile tfw_results_t r; tfw_regwin_init(&w, 0);
    memset((void *)&r, 0, sizeof r);
    w.axis_mask = (uint16_t)mask; w.freq = freq; w.count = count; w.seed = seed; w.repeat = repeat;
    G_TGT[0] = G_TGT[1] = 0;
    /* the fake machine's pcnt differs from the target after the first move: only the target sums are used */
    const tfw_seq_hw_t hw = {NULL, f_move, f_fixed, f_busy, f_count, f_stop, f_delay, f_now, f_abort};
    tfw_seq_run(&hw, &w, &r, (uint16_t)cmd);
    out[0] = G_TGT[0]; out[1] = G_TGT[1];
}
