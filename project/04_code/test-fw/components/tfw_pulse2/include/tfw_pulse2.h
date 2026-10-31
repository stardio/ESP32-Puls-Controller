/* TFW-02 tfw_pulse2: two-axis MCPWM + PCNT pulse generation (G3 pre-test firmware, D-025). */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/mcpwm_prelude.h"

typedef enum { TFW_UPD_TASK_10MS = 0, TFW_UPD_HWTIMER = 1 } tfw_upd_mode_t;

typedef struct {
    uint32_t isr_lat_max_ns;     /* ESTIMATE: pulses emitted between watch point and ISR x period (1 pulse resolution). Use the debug GPIO + logic analyser for the real value */
    uint32_t upd_max_us;         /* longest update-callback body (all axes) */
    uint32_t upd_late;           /* intervals > 1.5 x period */
    uint32_t freq_actual;        /* res / ticks of the last period written (always <= request) */
    uint32_t last_ticks;
    uint32_t max_period_ticks;   /* limit used for rejecting too-low frequencies */
} tfw_pulse_stats_t;

typedef struct {
    bool group1_ok, gen2_ok, pcnt_ch2_ok, etm_pcnt_ok;
    bool etm_tested;
    uint32_t max_period_ticks;
    int32_t err_group1, err_gen2, err_pcnt_ch2, err_etm;
} tfw_caps_t;

void      tfw_pulse2_probe_caps(tfw_caps_t *out);   /* call BEFORE init: creates and deletes test resources */
void      tfw_pulse2_set_cpu_mhz(uint32_t mhz);     /* measured CPU clock for the update timing statistics, before init */
void      tfw_pulse2_set_block_fn(bool (*fn)(uint8_t axis)); /* N-03: start of that axis refused / re-checked while fn(axis) is true (its STOP latched) */
void      tfw_pulse2_set_update_core(int core);     /* TASK mode pinning; -1 = unpinned */
esp_err_t tfw_pulse2_init(uint8_t axis_count, uint32_t res_hz, uint32_t upd_us, tfw_upd_mode_t mode);
bool      tfw_pulse2_axis_ok(uint8_t axis);
int       tfw_pulse2_axis_err(uint8_t axis);
void      tfw_pulse2_set_dir(uint8_t axis, bool forward);
esp_err_t tfw_pulse2_move(uint8_t axis, uint32_t speed_hz, uint32_t count, uint32_t accel_ms, uint32_t decel_ms);
esp_err_t tfw_pulse2_run_fixed(uint8_t axis, uint32_t freq_hz, uint32_t count /* 0 = endless */);
void      tfw_pulse2_stop(uint8_t axis);            /* force low + timer stop; ISR-safe */
bool      tfw_pulse2_busy(uint8_t axis);            /* finalises a finished move */
uint32_t  tfw_pulse2_count(uint8_t axis);
uint32_t  tfw_pulse2_count_isr(uint8_t axis);       /* ISR-safe variant */
void      tfw_pulse2_stats(uint8_t axis, tfw_pulse_stats_t *out);
void      tfw_pulse2_clear_stats(void);
mcpwm_gen_handle_t  tfw_pulse2_generator(uint8_t axis);
mcpwm_oper_handle_t tfw_pulse2_operator(uint8_t axis);
uint32_t  tfw_pulse2_res_hz(void);
