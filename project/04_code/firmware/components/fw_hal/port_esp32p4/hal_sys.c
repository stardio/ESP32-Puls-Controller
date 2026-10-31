/* fw_hal system services for ESP32-P4: time, delays, reset reason (T-15), task watchdog, reboot, MAC. */
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "fw_cfg.h"
#include "fw_hal.h"
#include "fw_params.h"

static const char *TAG = "FW_SYS";

uint32_t fw_hal_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
void fw_hal_delay_us(uint32_t us) { if (us) esp_rom_delay_us(us); }

uint16_t fw_hal_reset_reason(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return FW_RST_POWERON;
    case ESP_RST_SW:        return FW_RST_SOFTWARE;
    case ESP_RST_PANIC:     return FW_RST_PANIC;
    case ESP_RST_INT_WDT:   return FW_RST_INT_WDT;
    case ESP_RST_TASK_WDT:  return FW_RST_TASK_WDT;
    case ESP_RST_WDT:       return FW_RST_OTHER_WDT;
    case ESP_RST_BROWNOUT:  return FW_RST_BROWNOUT;
    case ESP_RST_DEEPSLEEP: return FW_RST_DEEPSLEEP;
    case ESP_RST_EXT:       return FW_RST_EXT;
    case ESP_RST_UNKNOWN:   return FW_RST_UNKNOWN;
    default:                return FW_RST_OTHER;
    }
}

/* Task watchdog: only the motion task is subscribed (idle tasks are not: a busy network stack must not reset the
 * controller), a stall of the motion task or an interrupt-watchdog timeout resets the chip. The reset releases GPIO33
 * (external 10 kOhm pull-down) so the pulse output is forbidden again. Timeout: CONFIG_FW_WDT_TIMEOUT_MS [assumed 1000]. */
int fw_hal_wdt_init(void)
{
    const esp_task_wdt_config_t cfg = {
        .timeout_ms = CONFIG_FW_WDT_TIMEOUT_MS,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    esp_err_t e = esp_task_wdt_reconfigure(&cfg);
    if (e == ESP_ERR_INVALID_STATE) e = esp_task_wdt_init(&cfg);       /* not initialised by the system yet */
    if (e != ESP_OK) { ESP_LOGE(TAG, "task watchdog: 0x%x", (unsigned)e); return (int)e; }
    ESP_LOGI(TAG, "task watchdog %d ms, panic -> reset", CONFIG_FW_WDT_TIMEOUT_MS);
    return 0;
}
int fw_hal_wdt_add_self(void) { return esp_task_wdt_add(NULL) == ESP_OK ? 0 : -1; }
void fw_hal_wdt_feed(void) { esp_task_wdt_reset(); }
void fw_hal_reboot(void) { esp_restart(); }
void fw_hal_mac(uint8_t mac[6]) { esp_read_mac(mac, ESP_MAC_ETH); }
