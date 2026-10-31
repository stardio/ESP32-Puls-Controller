#include "fw_modbus.h"
#include "esp_log.h"
#include "fw_cfg.h"
#include "fw_modbus_priv.h"
#include "fw_motion.h"
#include "fw_params.h"

static const char *TAG = "FW_MODBUS";

esp_err_t fw_modbus_start(void)
{
    const uint16_t mode = fw_motion_comm_mode();
    if (mode == FW_MODE_RTU) {
#if FW_B_MODEL_RTU
        ESP_LOGI(TAG, "mode RTU slave (TCP server not started)");
        return fw_mb_rtu_start();
#else
        ESP_LOGE(TAG, "RTU selected on a TCP-only model: refused");      /* fw_mode_effective() already prevents this */
        return ESP_ERR_NOT_SUPPORTED;
#endif
    }
    ESP_LOGI(TAG, "mode TCP server (RTU not started)");
    esp_err_t e = fw_net_start();
    if (e != ESP_OK) return e;
    return fw_mb_tcp_start();
}
