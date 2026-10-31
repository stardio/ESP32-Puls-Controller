/* TFW-04 tfw_estop_io: per-axis STOP (GPIO19/17) + START edge (GPIO54/18) (path A = ISR, path B = MCPWM fault brake). */
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

esp_err_t tfw_estop_init(const int gpio[2], bool active_low, mcpwm_oper_handle_t oper[2]);   /* STOP pin per axis; -1 = none */
void      tfw_estop_set_path(uint8_t sel);      /* 1 A, 2 B, 3 A+B */
uint16_t  tfw_estop_release(void);              /* all latched axes, independently; 0 ok, 0x10 first refusal */
uint16_t  tfw_estop_release_axis(uint8_t axis);
bool      tfw_estop_latched_axis(uint8_t axis);
bool      tfw_estop_blocking_axis(uint8_t axis);  /* axis STOP latched || its input active, or not initialised */
void      tfw_estop_set_start_cb(void (*cb)(uint8_t axis));  /* called from the watch task on an accepted START edge */
uint16_t  tfw_estop_start_edges(uint8_t axis);
uint16_t  tfw_estop_start_ignored(uint8_t axis);
bool      tfw_estop_latched(void);
bool      tfw_estop_input_active(uint8_t axis);
bool      tfw_estop_blocking(void);             /* latched || input active (M-04) */
void      tfw_estop_set_cpu_mhz(uint32_t mhz);  /* measured clock, call before init (m-02) */
void      tfw_estop_stats(tfw_es_stats_t *out);
void      tfw_estop_poll_once(void);            /* one 1 ms pass: input sampling, START edges, 10 ms clean-up (task body, host-testable) */
