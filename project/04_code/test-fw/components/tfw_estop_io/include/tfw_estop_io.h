/* TFW-04 tfw_estop_io: GPIO19 emergency stop (path A = ISR, path B = MCPWM fault brake). */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/mcpwm_prelude.h"

typedef struct {
    uint16_t state;            /* 0 normal, 1 input active, 2 latched / waiting release */
    uint16_t trip_count;
    uint16_t bind_ok;          /* 0 none, 1 group 0 only, 2 group 1 only, 3 both */
    uint16_t boot_level;       /* raw GPIO level read at init */
    uint16_t last_err;         /* low 16 bits of the first failing esp_err_t */
    uint32_t isr_lat_ns;       /* 0xFFFFFFFF = not measured inside the firmware (use debug GPIO + logic analyser) */
    uint32_t pulses_after[2];  /* PCNT 10 ms after trip minus PCNT snapshot in the ISR */
} tfw_es_stats_t;

esp_err_t tfw_estop_init(int gpio, bool active_low, mcpwm_oper_handle_t oper[2]);
void      tfw_estop_set_path(uint8_t sel);      /* 1 A, 2 B, 3 A+B */
uint16_t  tfw_estop_release(void);              /* 0 ok, 0x10 refused */
bool      tfw_estop_latched(void);
bool      tfw_estop_input_active(void);
void      tfw_estop_stats(tfw_es_stats_t *out);
