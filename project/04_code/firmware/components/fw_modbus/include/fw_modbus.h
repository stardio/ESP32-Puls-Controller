/* fw_modbus: transports for the register map. Exactly one runs per boot, chosen by the stored comm_mode (D-056, D-059):
 *  TCP: IP101 Ethernet, static IPv4, Modbus TCP server (default 192.168.0.220:502)
 *  RTU: RS-485 slave on UART (TX 24, RX 47, DE 48), only on FW_MODEL_RTU hardware. */
#pragma once
#include "esp_err.h"
esp_err_t fw_modbus_start(void);
