/* Modbus RTU slave on RS-485 (ISO1500, TX 24 / RX 47 / DE 48). Pattern: servo1axis movion_modbus_rtu_master.c (UART1,
 * uart_set_mode RS485 half duplex with RTS = DE). The register map and commands are the same as TCP (fw_motion_process_pdu).
 * Frame end = silence of 3.5 characters (fixed 2 ms above 19200 baud, tick resolution 1 ms). NOT hardware tested. */
#include <string.h>
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fw_cfg.h"
#include "fw_modbus_priv.h"
#include "fw_motion.h"
#include "fw_params.h"

static const char *TAG = "FW_RTU";
#define UART_NUM ((uart_port_t)CONFIG_FW_RTU_UART_NUM)
#define RTU_MAX_FRAME 256
static uint8_t s_addr;
static uint32_t s_gap_ms;

/* returns the length of a complete valid frame addressed to us (including address and CRC), 0 otherwise */
static size_t rtu_receive(uint8_t *buf)
{
    size_t total = 0;
    int n = uart_read_bytes(UART_NUM, buf, 1, pdMS_TO_TICKS(100));
    if (n != 1) return 0;
    total = 1;
    while ((n = uart_read_bytes(UART_NUM, buf + total, RTU_MAX_FRAME - total, pdMS_TO_TICKS(s_gap_ms))) > 0) {
        total += (size_t)n;
        if (total >= RTU_MAX_FRAME) { uart_flush_input(UART_NUM); return 0; }
    }
    if (total < 4) return 0;
    const uint16_t crc = fw_crc16_modbus(buf, total - 2);
    if (buf[total - 2] != (uint8_t)crc || buf[total - 1] != (uint8_t)(crc >> 8)) return 0;      /* CRC low byte first */
    if (buf[0] != s_addr && buf[0] != 0) return 0;
    return total;
}

static void rtu_task(void *arg)
{
    (void)arg;
    uint8_t buf[RTU_MAX_FRAME], resp[FW_MB_MAX_PDU], out[RTU_MAX_FRAME + 4];
    for (;;) {
        const size_t n = rtu_receive(buf);
        if (!n) continue;
        const bool broadcast = buf[0] == 0;
        const uint8_t fc = buf[1];
        if (broadcast && !(fc == 5 || fc == 6 || fc == 15 || fc == 16)) continue;     /* broadcast: writes only, no reply */
        const size_t rl = fw_motion_process_pdu(1, buf + 1, n - 3, resp);
        if (broadcast || !rl) continue;
        out[0] = s_addr;
        memcpy(out + 1, resp, rl);
        const uint16_t crc = fw_crc16_modbus(out, rl + 1);
        out[rl + 1] = (uint8_t)crc; out[rl + 2] = (uint8_t)(crc >> 8);
        uart_write_bytes(UART_NUM, out, rl + 3);
        uart_wait_tx_done(UART_NUM, pdMS_TO_TICKS(100));
    }
}

esp_err_t fw_mb_rtu_start(void)
{
    uint16_t net[FW_NET_WORDS];
    fw_motion_net_words(net);
    s_addr = (uint8_t)net[FW_N_RTU_ADDR];
    const uint32_t baud = (uint32_t)net[FW_N_RTU_BAUD] * 100u;     /* register = baud / 100 */
    s_gap_ms = baud > 19200 ? 2u : (38500u / baud + 1u);
    const uart_config_t uc = {
        .baud_rate = (int)baud, .data_bits = UART_DATA_8_BITS,
        .parity = net[FW_N_RTU_PARITY] == 1 ? UART_PARITY_EVEN : net[FW_N_RTU_PARITY] == 2 ? UART_PARITY_ODD : UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1, .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t e = uart_driver_install(UART_NUM, 512, 0, 0, NULL, 0);
    if (e != ESP_OK) return e;
    e = uart_param_config(UART_NUM, &uc);
    if (e == ESP_OK) e = uart_set_pin(UART_NUM, CONFIG_FW_RS485_TX_GPIO, CONFIG_FW_RS485_RX_GPIO, CONFIG_FW_RS485_DE_GPIO, UART_PIN_NO_CHANGE);
    if (e == ESP_OK) e = uart_set_mode(UART_NUM, UART_MODE_RS485_HALF_DUPLEX);
    if (e != ESP_OK) { ESP_LOGE(TAG, "UART setup failed: 0x%x", (unsigned)e); return e; }
    ESP_LOGI(TAG, "RTU slave address %u, %lu baud, parity %u, 8 data bits, 1 stop bit", (unsigned)s_addr, (unsigned long)baud, (unsigned)net[FW_N_RTU_PARITY]);
    return xTaskCreate(rtu_task, "fw_rtu", 4096, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
