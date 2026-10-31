/* Shared between the port_esp32p4 files only. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "driver/mcpwm_prelude.h"

#define FW_MAX_AXES_HAL 2

bool                fw_port_stop_blocking(uint8_t axis);          /* hal_io.c: latched || active || io not ready */
uint32_t            fw_port_count_isr(uint8_t axis);              /* hal_pulse.c, IRAM */
mcpwm_gen_handle_t  fw_port_generator(uint8_t axis);
mcpwm_oper_handle_t fw_port_operator(uint8_t axis);
