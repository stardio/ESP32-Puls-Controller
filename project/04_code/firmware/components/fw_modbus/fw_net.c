/* IP101 Ethernet with a static IPv4 address. Source: servo1axis movion_modbus_tcp.c movion_modbus_network_start()
 * (IP101 part only; the optional W5500 is not used by the product, D-041). NOT built here. */
#include "esp_check.h"
#include "esp_eth.h"
#include "esp_eth_mac_esp.h"
#include "esp_eth_netif_glue.h"
#include "esp_eth_phy.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/ip4_addr.h"
#include "fw_cfg.h"
#include "fw_hal.h"
#include "fw_modbus_priv.h"
#include "fw_motion.h"
#include "fw_params.h"

static const char *TAG = "FW_NET";
static esp_eth_handle_t s_eth;
static esp_netif_t *s_netif;

static void on_eth(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    if (id == ETHERNET_EVENT_CONNECTED) { fw_motion_set_link(true); ESP_LOGI(TAG, "link up"); }
    else if (id == ETHERNET_EVENT_DISCONNECTED) { fw_motion_set_link(false); ESP_LOGW(TAG, "link down"); }
}

static void ip4_from_u32(esp_ip4_addr_t *out, uint32_t v) { IP4_ADDR(out, v >> 24, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF); }

esp_err_t fw_net_start(void)
{
    uint16_t net[FW_NET_WORDS];
    fw_motion_net_words(net);
    uint8_t mac[6];
    fw_hal_mac(mac);
    fw_motion_set_mac(mac);

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    esp_err_t le = esp_event_loop_create_default();
    ESP_RETURN_ON_FALSE(le == ESP_OK || le == ESP_ERR_INVALID_STATE, le, TAG, "event loop");

    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.phy_addr = 1;
    phy_cfg.reset_gpio_num = CONFIG_FW_PHY_RESET_GPIO;
    eth_esp32_emac_config_t emac = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    esp_eth_mac_t *m = esp_eth_mac_new_esp32(&emac, &mac_cfg);
    ESP_RETURN_ON_FALSE(m, ESP_ERR_NO_MEM, TAG, "EMAC");
    esp_eth_phy_t *p = esp_eth_phy_new_ip101(&phy_cfg);
    ESP_RETURN_ON_FALSE(p, ESP_ERR_NO_MEM, TAG, "IP101");
    esp_eth_config_t cfg = ETH_DEFAULT_CONFIG(m, p);
    ESP_RETURN_ON_ERROR(esp_eth_driver_install(&cfg, &s_eth), TAG, "driver install");

    esp_netif_config_t ncfg = ESP_NETIF_DEFAULT_ETH();
    s_netif = esp_netif_new(&ncfg);
    ESP_RETURN_ON_FALSE(s_netif, ESP_ERR_NO_MEM, TAG, "netif");
    esp_eth_netif_glue_handle_t glue = esp_eth_new_netif_glue(s_eth);
    ESP_RETURN_ON_FALSE(glue, ESP_ERR_NO_MEM, TAG, "glue");
    ESP_RETURN_ON_ERROR(esp_netif_attach(s_netif, glue), TAG, "attach");
    esp_err_t de = esp_netif_dhcpc_stop(s_netif);
    ESP_RETURN_ON_FALSE(de == ESP_OK || de == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED, de, TAG, "dhcp stop");

    esp_netif_ip_info_t ip = {0};
    ip4_from_u32(&ip.ip, fw_w32(net, FW_N_IP));
    ip4_from_u32(&ip.netmask, fw_w32(net, FW_N_MASK));
    ip4_from_u32(&ip.gw, fw_w32(net, FW_N_GW));
    ESP_RETURN_ON_ERROR(esp_netif_set_ip_info(s_netif, &ip), TAG, "static ip");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, on_eth, NULL), TAG, "events");
    ESP_RETURN_ON_ERROR(esp_eth_start(s_eth), TAG, "start");
    ESP_LOGI(TAG, "static IPv4 " IPSTR, IP2STR(&ip.ip));
    return ESP_OK;
}
