/* fw_motion / fw_axis: per-axis motion state machine (positioning, decel stop, quick stop, STOP latch handling,
 * servo ON, software position). Uses only fw_core and fw_hal.h, so it runs on the host against a fake HAL.
 * Not thread safe: fw_motion.c calls it with the device lock held. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "fw_hal.h"
#include "fw_regmap.h"

/* boot_status bits that must all be 1 before pulses may be enabled (plus the axis' own PULSE bit) */
#define FW_BOOT_OPERATE (FW_BOOT_STOP_IO | FW_BOOT_NVS | FW_BOOT_CFG_OK | FW_BOOT_WDT)

typedef enum { MV_IDLE = 0, MV_RUN, MV_DECEL } fw_mv_t;

typedef struct {
    fw_dev_t *dev;
    uint8_t   idx;
    fw_mv_t   mv;
    fw_profile_t prof;
    uint32_t  target_pulses, stop_at, last_hz, res_hz;
    bool      forward_positive;          /* direction of the running move (+ = position increases) */
    int32_t   position;                  /* software position at the start of the running move / when idle */
    bool      pos_valid;
    bool      servo_out;
    bool      done_ok;                   /* last move completed normally (axis_status b2) */
    uint16_t  user_cause;                /* stop cause requested while running */
    bool      prev_blocking;
    uint16_t  sticky_alarm, init_alarm;  /* alarm_code shown = 0x0101 while the STOP blocks, else sticky_alarm */
} fw_axis_t;

void    fw_axis_init(fw_axis_t *a, fw_dev_t *dev, uint8_t idx, uint32_t res_hz);
uint8_t fw_axis_command(fw_axis_t *a, uint16_t cmd);        /* axis_command semantic, returns/stores cmd_result */
uint8_t fw_axis_start_legacy(fw_axis_t *a);                 /* coil 0: relative move from the legacy values (axis X) */
uint8_t fw_axis_start_physical(fw_axis_t *a);               /* START edge: X = legacy start, Y = START_REL from target_position */
void    fw_axis_legacy_reset(fw_axis_t *a);                 /* coil 2 extra: current value, result, fault := 0 */
void    fw_axis_poll(fw_axis_t *a);                         /* every FW_UPDATE_MS */
void    fw_axis_decel_all_reason(fw_axis_t *a, uint16_t alarm_code);   /* system-initiated decel stop (comm loss) */
bool    fw_axis_running(const fw_axis_t *a);
uint16_t fw_axis_sticky_alarm(const fw_axis_t *a);
void    fw_axis_servo_boot(fw_axis_t *a);                   /* svon_at_boot */
