/* Pulse servo controller, ESP32-P4 (ESP-IDF 5.5.3): 1 or 2 axes, Modbus TCP or RTU. See notes/firmware.md.
 * Structure follows servo1axis (app_main -> motion + Modbus) with the hardware behind fw_hal. */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fw_cfg.h"
#include "fw_hal.h"
#include "fw_modbus.h"
#include "fw_motion.h"

static const char *TAG = "FW_MAIN";

void app_main(void)
{
    /* first statement: pulse output forbidden (GPIO33 LOW) until every initialisation step succeeded */
    fw_hal_oe_early_forbid();

    ESP_LOGI(TAG, "MOVION pulse servo controller, %d axis, %s model", FW_AXIS_COUNT, FW_B_MODEL_RTU ? "RTU" : "TCP");
    const fw_motion_cfg_t cfg = {
        .axis_count = FW_AXIS_COUNT,
        .model_rtu = FW_B_MODEL_RTU != 0,
        .fw_version = ((uint32_t)CONFIG_FW_VERSION_MAJOR << 16) | ((uint32_t)CONFIG_FW_VERSION_MINOR << 8) | CONFIG_FW_VERSION_PATCH,
    };
    const int rc = fw_motion_start(&cfg);
    if (rc != 0) ESP_LOGE(TAG, "pulse output stays FORBIDDEN (init rc=%d); registers remain readable", rc);

    /* the transport also starts when operation is prohibited: the HMI must be able to show why */
    const esp_err_t me = fw_modbus_start();
    if (me != ESP_OK) ESP_LOGE(TAG, "Modbus start failed: %s", esp_err_to_name(me));

    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
