/* Pulse servo controller, ESP32-P4 (ESP-IDF 5.5.3): 1 or 2 axes, Modbus TCP or RTU. See notes/firmware.md.
 * Structure follows servo1axis (app_main -> motion + Modbus) with the hardware behind fw_hal. */
#include <string.h>
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

#if CONFIG_FW_DIAG_STACK_REPORT
    /* diagnostic build only (sdkconfig.diag, G2 condition C1 / I-049): print a task's stack high-water mark (bytes never
     * used) whenever it reaches a new low. Needs FREERTOS_USE_TRACE_FACILITY (set by the fragment). */
    static TaskStatus_t st[24];
    static struct { char name[configMAX_TASK_NAME_LEN]; uint32_t low; } seen[24];
    unsigned nseen = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(200));
        const UBaseType_t n = uxTaskGetSystemState(st, 24, NULL);
        for (UBaseType_t i = 0; i < n; ++i) {
            const uint32_t free_b = (uint32_t)st[i].usStackHighWaterMark * sizeof(StackType_t);
            unsigned k = 0;
            while (k < nseen && strncmp(seen[k].name, st[i].pcTaskName, sizeof(seen[k].name)) != 0) ++k;
            if (k == nseen && nseen < 24) { strncpy(seen[k].name, st[i].pcTaskName, sizeof(seen[k].name) - 1); seen[k].low = UINT32_MAX; ++nseen; }
            if (k < nseen && free_b < seen[k].low) {
                seen[k].low = free_b;
                ESP_LOGW("FW_STACK", "%-12s stack never used: %u bytes (new low)", st[i].pcTaskName, (unsigned)free_b);
            }
        }
    }
#else
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
#endif
}
