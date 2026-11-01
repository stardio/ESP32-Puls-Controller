/* fw_core / fw_unit: unit-mode conversion (FR-029), integer only, no hardware. interface-spec 5.12.
 * unit values are in 1/1000 of the unit per interface-spec: mm mode = um, degree mode = mdeg, speed = (um or mdeg)/s.
 * unit_mode 0 (pulse): the conversion is the identity (unit value = pulses, speed = Hz).
 * pulses = round_half_away(unit * pulses_per_rev * gear_num / (travel_per_load_rev * gear_den)), travel_per_load_rev =
 * lead_um (mm) or 360000 mdeg (degree). 128-bit intermediate product, so no overflow for any legal parameter set;
 * results outside the 32-bit target range return false. Error of unit->pulses is <= 0.5 pulse. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { FW_UM_PULSE = 0, FW_UM_MM = 1, FW_UM_DEG = 2 };
#define FW_DEG_MDEG_PER_REV 360000u

/* par = axis parameter segment (FW_P_UNIT_MODE etc. offsets in fw_params.h) */
bool fw_unit_to_pulses(const uint16_t *par, int64_t unit, int32_t *pulses);         /* false: overflow of S32 */
bool fw_unit_speed_to_hz(const uint16_t *par, uint32_t speed_unit, uint32_t *hz);   /* false: overflow of U32 */
bool fw_pulses_to_unit(const uint16_t *par, int64_t pulses, int32_t *unit);         /* false: overflow of S32 */
bool fw_hz_to_unit(const uint16_t *par, uint32_t hz, uint32_t *speed_unit);         /* false: overflow of U32 */
int32_t fw_pulses_to_unit_sat(const uint16_t *par, int32_t pulses);                 /* saturating, for the position_unit register */
/* highest rpm reachable with min(1 MHz, max_speed) at pulses_per_rev (FR-030), clamped to 65535 */
uint16_t fw_max_rpm_calc(const uint16_t *par);
/* TEST_POS/NEG speed in Hz from test_rpm and pulses_per_rev; false if it is outside 100 Hz .. min(1 MHz, max_speed) */
bool fw_test_hz(const uint16_t *par, uint32_t *hz);
