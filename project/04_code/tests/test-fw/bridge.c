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
