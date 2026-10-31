#pragma once
#include <stdint.h>
#include "esp_err.h"
esp_err_t fw_net_start(void);                                 /* IP101 + static IPv4 from the stored network words */
esp_err_t fw_mb_tcp_start(void);
esp_err_t fw_mb_rtu_start(void);
