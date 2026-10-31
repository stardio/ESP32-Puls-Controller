/* fw_hal non-volatile storage: one NVS blob. Writes happen only from fw_motion's deferred save (never per register write,
 * never while an axis runs). Flash-write interference with the pulse ISRs is handled by the IRAM-safe driver options in
 * sdkconfig.defaults. */
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "fw_hal.h"

static const char *TAG = "FW_STORE";
#define NS "fw"
#define KEY "cfg"

int fw_hal_store_init(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase (%s)", esp_err_to_name(e));
        e = nvs_flash_erase();
        if (e == ESP_OK) e = nvs_flash_init();
    }
    return e == ESP_OK ? 0 : (int)e;
}

int fw_hal_store_read(void *buf, size_t *len)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READONLY, &h);
    if (e == ESP_ERR_NVS_NOT_FOUND) return FW_ST_NOT_FOUND;
    if (e != ESP_OK) return FW_ST_ERR;
    e = nvs_get_blob(h, KEY, buf, len);
    nvs_close(h);
    if (e == ESP_ERR_NVS_NOT_FOUND) return FW_ST_NOT_FOUND;
    if (e == ESP_ERR_NVS_INVALID_LENGTH) return FW_ST_BAD_SIZE;      /* a blob of another size: layout changed or damaged */
    return e == ESP_OK ? FW_ST_OK : FW_ST_ERR;
}

int fw_hal_store_write(const void *buf, size_t len)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return FW_ST_ERR;
    e = nvs_set_blob(h, KEY, buf, len);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) ESP_LOGE(TAG, "write failed: %s", esp_err_to_name(e));
    return e == ESP_OK ? FW_ST_OK : FW_ST_ERR;
}
