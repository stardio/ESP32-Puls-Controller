/* TFW-03 tfw_modbus: IP101 Modbus TCP server with legacy window (HR 0..31, coils 0..7, DI 0..7) and test window. */
#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "tfw_logic.h"

typedef struct {
    uint16_t clients;
    uint16_t link_down_count;
    uint32_t req_count;
    uint32_t proc_max_us;
    uint16_t hist[8];
    uint32_t rejected_connections;   /* extra (not in the register map): 5th+ connection closed */
} tfw_mb_stats_t;

typedef struct {
    tfw_regwin_t *win;
    void (*get_results)(void *user, tfw_results_t *out);     /* called under the Modbus lock */
    void (*on_effect)(void *user, const tfw_wr_effect_t *fx); /* called after the lock is released */
    void *user;
} tfw_mb_hooks_t;

void      tfw_modbus_set_hooks(const tfw_mb_hooks_t *hooks);   /* before start */
void      tfw_modbus_set_core(int core);                        /* -1 unpinned (before start) */
esp_err_t tfw_modbus_start(const uint8_t ip[4], uint16_t port, uint8_t unit_id, uint8_t max_clients);
void      tfw_modbus_get_stats(tfw_mb_stats_t *out);
void      tfw_modbus_clear_stats(void);
