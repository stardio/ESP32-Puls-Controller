/* TFW-03. Copied and reduced from servo1axis components/movion_modbus/movion_modbus_tcp.c [E2]:
 *   copied: receive_exact/send_exact, MBAP handling in client_task, server_task, IP101 EMAC bring-up
 *           (PHY addr 1, reset GPIO51, static IP, DHCP off), ethernet/got-ip event handlers,
 *           servo output group GPIO23/26/27/46, reduced motion_task.
 *   removed: W5500 path (D-027), NVS save on every write (risk 3, see tfw_mb.c / CONFIG_TFW_LEGACY_NVS_ON_WRITE).
 *   PDU handling lives in tfw_logic/tfw_mb.c (host-tested).
 * NOT verified on hardware. */
#include "tfw_modbus.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_eth.h"
#include "esp_eth_mac_esp.h"
#include "esp_eth_netif_glue.h"
#include "esp_eth_phy.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "tfw_estop_io.h"
#include "tfw_pulse2.h"

static const char *TAG = "TFW_MODBUS";

#define SERVO_OUTPUT_MASK ((1ULL << GPIO_NUM_23) | (1ULL << GPIO_NUM_26) | (1ULL << GPIO_NUM_27) | (1ULL << GPIO_NUM_46))   /* DO1..DO4 */

static SemaphoreHandle_t s_lock;
static tfw_legacy_t s_legacy;
static tfw_mb_ctx_t s_ctx;
static tfw_mb_hooks_t s_hooks;
static tfw_mb_stats_t s_stats;
static uint8_t s_unit, s_max_clients;
static uint16_t s_port;
static int s_core = -1;
static volatile bool s_link_up;
static volatile unsigned s_clients;
static bool s_moving;
static uint32_t s_active_pos;
static esp_eth_handle_t s_eth;
static esp_netif_t *s_netif;

void tfw_modbus_set_hooks(const tfw_mb_hooks_t *h) { s_hooks = *h; }
void tfw_modbus_set_core(int core) { s_core = core; }

static BaseType_t create_task(TaskFunction_t fn, const char *name, uint32_t stack, void *arg, UBaseType_t prio)
{
    return (s_core >= 0) ? xTaskCreatePinnedToCore(fn, name, stack, arg, prio, NULL, s_core)
                         : xTaskCreate(fn, name, stack, arg, prio, NULL);
}

static void get_results_cb(void *user, tfw_results_t *out)
{
    if (s_hooks.get_results) s_hooks.get_results(s_hooks.user, out);
    /* Modbus fields are owned by this module and always fresh (lock is held by the caller) */
    out->mb_clients = s_stats.clients; out->mb_link_down = s_stats.link_down_count;
    out->mb_req_count = s_stats.req_count; out->mb_proc_max_us = s_stats.proc_max_us;
    memcpy(out->mb_hist, s_stats.hist, sizeof(out->mb_hist));
}

void tfw_modbus_get_stats(tfw_mb_stats_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_stats;
    xSemaphoreGive(s_lock);
}

void tfw_modbus_clear_stats(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint16_t cl = s_stats.clients, ld = s_stats.link_down_count;
    memset(&s_stats, 0, sizeof(s_stats));
    s_stats.clients = cl; s_stats.link_down_count = ld;
    xSemaphoreGive(s_lock);
}

/* ---------------------------------------------------------------- legacy servo outputs and persistence */
static void set_servo_outputs(bool on)
{
    /* F-09: DO1 (23) = axis 1, DO2 (26) = axis 2, DO3 (27) / DO4 (46) always off. The legacy HMI servo coil switches both axes. */
    const tfw_servo_do_t d = tfw_servo_do(on, on);
    gpio_set_level(GPIO_NUM_23, d.do1); gpio_set_level(GPIO_NUM_26, d.do2);
    gpio_set_level(GPIO_NUM_27, d.do3); gpio_set_level(GPIO_NUM_46, d.do4);
}

#if CONFIG_TFW_LEGACY_NVS_ON_WRITE
/* Reproduces servo1axis: NVS save after each accepted write. Off by default (risk 3). */
static void persist_legacy(void)
{
    uint16_t copy[TFW_HR_LEGACY_COUNT];
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memcpy(copy, s_legacy.hr, sizeof(copy));
    s_legacy.dirty = false;
    xSemaphoreGive(s_lock);
    nvs_handle_t h;
    if (nvs_open("tfw_legacy", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "hr", copy, sizeof(copy));
        nvs_commit(h);
        nvs_close(h);
    }
}
#endif

/* Reduced copy of servo1axis motion_task: START / FORCE_STOP+RESET / completion for axis 1 only. */
static void legacy_motion_task(void *arg)
{
    (void)arg;
    for (;;) {
        bool start, reset, fwd;
        uint32_t pos, speed, acc, dec;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        start = (s_legacy.coils >> TFW_COIL_START) & 1u;
        reset = (s_legacy.coils >> TFW_COIL_STOP_RESET) & 1u;
        fwd = (s_legacy.coils >> TFW_COIL_DIR_FWD) & 1u;
        s_legacy.coils &= (uint16_t)~((1u << TFW_COIL_START) | (1u << TFW_COIL_STOP_RESET));
        pos = tfw_legacy_u32(&s_legacy, TFW_HR_POS);
        speed = tfw_legacy_u32(&s_legacy, TFW_HR_SPEED);
        acc = tfw_legacy_u32(&s_legacy, TFW_HR_ACCEL);
        dec = tfw_legacy_u32(&s_legacy, TFW_HR_DECEL);
        set_servo_outputs((s_legacy.coils >> TFW_COIL_SERVO) & 1u);
        xSemaphoreGive(s_lock);

        if (s_moving) {
            if (reset) {
                tfw_pulse2_stop(0);
                s_moving = false;
                xSemaphoreTake(s_lock, portMAX_DELAY);
                s_legacy.hr[TFW_HR_RESULT] = 0; s_legacy.hr[TFW_HR_FAULT] = 0;
                tfw_legacy_store_u32(&s_legacy, TFW_HR_CUR, 0);
                xSemaphoreGive(s_lock);
            } else if (!tfw_pulse2_busy(0)) {
                uint32_t measured = tfw_pulse2_count(0);
                s_moving = false;
                xSemaphoreTake(s_lock, portMAX_DELAY);
                tfw_legacy_store_u32(&s_legacy, TFW_HR_CUR, measured);
                s_legacy.hr[TFW_HR_RESULT] = (measured == s_active_pos) ? 1 : 3;
                s_legacy.hr[TFW_HR_FAULT] = (measured == s_active_pos) ? 0 : 2;
                xSemaphoreGive(s_lock);
                ESP_LOGI(TAG, "legacy move done target=%lu output=%lu", (unsigned long)s_active_pos, (unsigned long)measured);
            }
        } else if (reset) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_legacy.hr[TFW_HR_RESULT] = 0; s_legacy.hr[TFW_HR_FAULT] = 0;
            tfw_legacy_store_u32(&s_legacy, TFW_HR_CUR, 0);
            xSemaphoreGive(s_lock);
        } else if (start) {
            esp_err_t e = ESP_ERR_INVALID_STATE;
            if (tfw_pulse2_axis_ok(0) && !tfw_estop_blocking_axis(0) && !(s_hooks.seq_busy && s_hooks.seq_busy())) {     /* M-04: latched or input still active */
                tfw_pulse2_set_dir(0, fwd);
                e = tfw_pulse2_move(0, speed, pos, acc, dec);
            }
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_legacy.hr[TFW_HR_RESULT] = 0;
            s_legacy.hr[TFW_HR_FAULT] = (e == ESP_OK) ? 0 : 1;
            tfw_legacy_store_u32(&s_legacy, TFW_HR_CUR, 0);
            xSemaphoreGive(s_lock);
            if (e == ESP_OK) { s_moving = true; s_active_pos = pos; }
            else ESP_LOGW(TAG, "legacy START rejected: %s", esp_err_to_name(e));
        }
        if (s_moving) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            tfw_legacy_store_u32(&s_legacy, TFW_HR_CUR, tfw_pulse2_count(0));
            xSemaphoreGive(s_lock);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ---------------------------------------------------------------- TCP */
static int receive_exact(int fd, uint8_t *d, size_t n)
{
    size_t off = 0;
    while (off < n) { int r = recv(fd, d + off, n - off, 0); if (r <= 0) return r; off += (size_t)r; }
    return (int)off;
}
static int send_exact(int fd, const uint8_t *d, size_t n)
{
    size_t off = 0;
    while (off < n) { int r = send(fd, d + off, n - off, 0); if (r <= 0) return r; off += (size_t)r; }
    return (int)off;
}

static void client_task(void *argument)
{
    const int fd = *(int *)argument;
    free(argument);
    struct timeval to = {.tv_sec = 5};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &to, sizeof(to));
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    ESP_LOGI(TAG, "client connected (%u)", s_clients);

    uint8_t hdr[7], req[TFW_MB_MAX_PDU], resp[TFW_MB_MAX_PDU], frame[7 + TFW_MB_MAX_PDU];
    for (;;) {
        if (receive_exact(fd, hdr, sizeof(hdr)) != (int)sizeof(hdr)) break;
        uint16_t proto = (uint16_t)((hdr[2] << 8) | hdr[3]), len = (uint16_t)((hdr[4] << 8) | hdr[5]);
        if (proto != 0 || len < 2 || len > TFW_MB_MAX_PDU + 1) break;
        size_t pdu = len - 1u;
        if (receive_exact(fd, req, pdu) != (int)pdu) break;

        const int64_t t0 = esp_timer_get_time();             /* receive complete */
        size_t rl;
        tfw_wr_effect_t fx;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (hdr[6] != s_unit) {
            resp[0] = req[0] | 0x80; resp[1] = 11; rl = 2;
        } else {
            s_ctx.link = s_link_up; s_ctx.clients = s_clients; s_ctx.moving = s_moving;
            memset(&s_ctx.fx, 0, sizeof(s_ctx.fx));
            rl = tfw_mb_process(&s_ctx, req, pdu, resp);
        }
        fx = s_ctx.fx;
        memset(&s_ctx.fx, 0, sizeof(s_ctx.fx));
        xSemaphoreGive(s_lock);
        if (!rl) break;
        if (s_hooks.on_effect && (fx.cmd || fx.es_path_set || fx.es_release || fx.cfg_changed || fx.load_changed || fx.t15_hang))
            s_hooks.on_effect(s_hooks.user, &fx);
#if CONFIG_TFW_LEGACY_NVS_ON_WRITE
        if (s_legacy.dirty) persist_legacy();
#endif
        memcpy(frame, hdr, 4);
        frame[4] = (uint8_t)((rl + 1) >> 8); frame[5] = (uint8_t)(rl + 1); frame[6] = hdr[6];
        memcpy(frame + 7, resp, rl);
        const uint32_t us = (uint32_t)(esp_timer_get_time() - t0);   /* receive complete -> just before send */
        if (send_exact(fd, frame, 7 + rl) <= 0) break;

        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_stats.req_count++;
        if (us > s_stats.proc_max_us) s_stats.proc_max_us = us;
        unsigned b = tfw_hist_bin(us);
        if (s_stats.hist[b] < 0xFFFF) s_stats.hist[b]++;
        xSemaphoreGive(s_lock);
    }
    shutdown(fd, SHUT_RDWR);
    close(fd);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_clients) s_clients--;
    s_stats.clients = (uint16_t)s_clients;
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "client disconnected (%u)", s_clients);
    vTaskDelete(NULL);
}

static void server_task(void *arg)
{
    (void)arg;
    for (;;) {
        int lfd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (lfd < 0) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
        int reuse = 1;
        setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(s_port), .sin_addr.s_addr = htonl(INADDR_ANY)};
        if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(lfd, 4) != 0) {
            ESP_LOGE(TAG, "listen failed errno %d", errno);
            close(lfd); vTaskDelay(pdMS_TO_TICKS(1000)); continue;
        }
        ESP_LOGI(TAG, "listening on port %u unit %u max_clients %u", s_port, s_unit, s_max_clients);
        for (;;) {
            struct sockaddr_storage src; socklen_t sl = sizeof(src);
            int cfd = accept(lfd, (struct sockaddr *)&src, &sl);
            if (cfd < 0) break;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            bool full = s_clients >= s_max_clients;
            if (full) s_stats.rejected_connections++;
            else { s_clients++; s_stats.clients = (uint16_t)s_clients; }
            xSemaphoreGive(s_lock);
            if (full) { ESP_LOGW(TAG, "connection refused: %u clients already", s_max_clients); close(cfd); continue; }
            int *p = malloc(sizeof(*p));
            if (!p) { close(cfd); xSemaphoreTake(s_lock, portMAX_DELAY); s_clients--; s_stats.clients = (uint16_t)s_clients; xSemaphoreGive(s_lock); continue; }
            *p = cfd;
            if (create_task(client_task, "mb_client", 4096, p, 6) != pdPASS) {
                close(cfd); free(p);
                xSemaphoreTake(s_lock, portMAX_DELAY); s_clients--; s_stats.clients = (uint16_t)s_clients; xSemaphoreGive(s_lock);
            }
        }
        close(lfd);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ---------------------------------------------------------------- IP101 Ethernet (copied from [E2]) */
static void eth_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    if (id == ETHERNET_EVENT_CONNECTED) { s_link_up = true; ESP_LOGI(TAG, "Ethernet link up"); }
    else if (id == ETHERNET_EVENT_DISCONNECTED) {
        s_link_up = false;
        if (s_lock) { xSemaphoreTake(s_lock, portMAX_DELAY); s_stats.link_down_count++; xSemaphoreGive(s_lock); }
        ESP_LOGW(TAG, "Ethernet link down");
    }
}
static void got_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)id;
    const ip_event_got_ip_t *ev = (const ip_event_got_ip_t *)data;
    ESP_LOGI(TAG, "IPv4 " IPSTR " mask " IPSTR " gw " IPSTR, IP2STR(&ev->ip_info.ip), IP2STR(&ev->ip_info.netmask), IP2STR(&ev->ip_info.gw));
}

esp_err_t tfw_modbus_start(const uint8_t ip[4], uint16_t port, uint8_t unit_id, uint8_t max_clients)
{
    ESP_RETURN_ON_FALSE(port && unit_id && max_clients, ESP_ERR_INVALID_ARG, TAG, "bad args");
    s_port = port; s_unit = unit_id; s_max_clients = max_clients;
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "mutex");
    tfw_legacy_init(&s_legacy);
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.legacy = &s_legacy; s_ctx.win = s_hooks.win; s_ctx.get_results = get_results_cb;
    ESP_RETURN_ON_FALSE(s_ctx.win, ESP_ERR_INVALID_STATE, TAG, "hooks not set");

    gpio_config_t outs = {.pin_bit_mask = SERVO_OUTPUT_MASK, .mode = GPIO_MODE_OUTPUT};
    ESP_RETURN_ON_ERROR(gpio_config(&outs), TAG, "servo outputs");
    set_servo_outputs(false);

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
    esp_err_t le = esp_event_loop_create_default();
    ESP_RETURN_ON_FALSE(le == ESP_OK || le == ESP_ERR_INVALID_STATE, le, TAG, "event loop");

    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.phy_addr = 1;
    phy_cfg.reset_gpio_num = 51;
    eth_esp32_emac_config_t emac = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac, &mac_cfg);
    esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&phy_cfg);
    ESP_RETURN_ON_FALSE(mac && phy, ESP_ERR_NO_MEM, TAG, "EMAC/PHY");
    esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
    ESP_RETURN_ON_ERROR(esp_eth_driver_install(&eth_cfg, &s_eth), TAG, "eth install");
    esp_netif_config_t nc = ESP_NETIF_DEFAULT_ETH();
    s_netif = esp_netif_new(&nc);
    ESP_RETURN_ON_FALSE(s_netif, ESP_ERR_NO_MEM, TAG, "netif new");
    esp_eth_netif_glue_handle_t glue = esp_eth_new_netif_glue(s_eth);
    ESP_RETURN_ON_FALSE(glue, ESP_ERR_NO_MEM, TAG, "glue");
    ESP_RETURN_ON_ERROR(esp_netif_attach(s_netif, glue), TAG, "attach");
    esp_err_t de = esp_netif_dhcpc_stop(s_netif);
    ESP_RETURN_ON_FALSE(de == ESP_OK || de == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED, de, TAG, "dhcp stop");
    esp_netif_ip_info_t info = {0};
    IP4_ADDR(&info.ip, ip[0], ip[1], ip[2], ip[3]);
    IP4_ADDR(&info.netmask, 255, 255, 255, 0);
    IP4_ADDR(&info.gw, 0, 0, 0, 0);
    ESP_RETURN_ON_ERROR(esp_netif_set_ip_info(s_netif, &info), TAG, "static ip");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, eth_event, NULL), TAG, "eth evt");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, got_ip_event, NULL), TAG, "ip evt");
    ESP_RETURN_ON_ERROR(esp_eth_start(s_eth), TAG, "eth start");

    ESP_RETURN_ON_FALSE(create_task(server_task, "mb_server", 5120, NULL, 5) == pdPASS, ESP_ERR_NO_MEM, TAG, "server task");
    ESP_RETURN_ON_FALSE(create_task(legacy_motion_task, "mb_legacy", 4096, NULL, 7) == pdPASS, ESP_ERR_NO_MEM, TAG, "legacy task");
    ESP_LOGI(TAG, "static %u.%u.%u.%u:%u unit %u", ip[0], ip[1], ip[2], ip[3], port, unit_id);
    return ESP_OK;
}
