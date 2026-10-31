/* Modbus TCP server. Source: servo1axis movion_modbus_tcp.c server_task()/client_task() (framing, unit-id handling,
 * 5 s socket timeout). Differences: the PDU is handled by fw_motion_process_pdu() (shared register map), the connection
 * count is limited by max_clients (0x0114, applied at boot), connection ids feed the comm-loss monitor. NOT built here. */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "fw_cfg.h"
#include "fw_modbus_priv.h"
#include "fw_motion.h"
#include "fw_params.h"

static const char *TAG = "FW_TCP";
#define PORT CONFIG_FW_TCP_PORT
static unsigned s_max_clients, s_clients;
static uint8_t s_unit;
static int32_t s_next_conn;

typedef struct { int fd; int32_t conn; } client_arg_t;

static uint16_t be16(const uint8_t *d) { return (uint16_t)(((uint16_t)d[0] << 8) | d[1]); }
static void put16(uint8_t *d, uint16_t v) { d[0] = (uint8_t)(v >> 8); d[1] = (uint8_t)v; }

static int recv_exact(int fd, uint8_t *d, size_t n)
{
    size_t off = 0;
    while (off < n) {
        const int r = recv(fd, d + off, n - off, 0);
        if (r <= 0) return r;
        off += (size_t)r;
    }
    return (int)off;
}
static int send_exact(int fd, const uint8_t *d, size_t n)
{
    size_t off = 0;
    while (off < n) {
        const int r = send(fd, d + off, n - off, 0);
        if (r <= 0) return r;
        off += (size_t)r;
    }
    return (int)off;
}

static void client_task(void *argument)
{
    client_arg_t arg = *(client_arg_t *)argument;
    free(argument);
    const unsigned n = __atomic_add_fetch(&s_clients, 1, __ATOMIC_SEQ_CST);
    fw_motion_set_clients(n);
    ESP_LOGI(TAG, "client %ld connected (%u)", (long)arg.conn, n);

    struct timeval tv = {.tv_sec = 5, .tv_usec = 0};
    setsockopt(arg.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(arg.fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    uint8_t hdr[7], req[FW_MB_MAX_PDU], resp[FW_MB_MAX_PDU], frame[7 + FW_MB_MAX_PDU];
    for (;;) {
        if (recv_exact(arg.fd, hdr, sizeof(hdr)) != (int)sizeof(hdr)) break;
        const uint16_t proto = be16(hdr + 2), len = be16(hdr + 4);
        if (proto != 0 || len < 2 || len > FW_MB_MAX_PDU + 1) break;
        const size_t pdu = len - 1u;
        if (recv_exact(arg.fd, req, pdu) != (int)pdu) break;
        size_t rl;
        if (hdr[6] != s_unit) { resp[0] = (uint8_t)(req[0] | 0x80); resp[1] = 0x0B; rl = 2; }
        else rl = fw_motion_process_pdu(arg.conn, req, pdu, resp);
        if (!rl) break;
        memcpy(frame, hdr, 4);
        put16(frame + 4, (uint16_t)(rl + 1));
        frame[6] = hdr[6];
        memcpy(frame + 7, resp, rl);
        if (send_exact(arg.fd, frame, 7 + rl) <= 0) break;
    }
    shutdown(arg.fd, SHUT_RDWR);
    close(arg.fd);
    fw_motion_conn_closed(arg.conn);
    fw_motion_set_clients(__atomic_sub_fetch(&s_clients, 1, __ATOMIC_SEQ_CST));
    ESP_LOGI(TAG, "client %ld disconnected", (long)arg.conn);
    vTaskDelete(NULL);
}

static void server_task(void *argument)
{
    (void)argument;
    for (;;) {
        const int lfd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (lfd < 0) { ESP_LOGE(TAG, "socket errno %d", errno); vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
        int one = 1;
        setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(PORT), .sin_addr.s_addr = htonl(INADDR_ANY)};
        if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(lfd, 4) != 0) {
            ESP_LOGE(TAG, "listen on port %d failed, errno %d", PORT, errno);
            close(lfd); vTaskDelay(pdMS_TO_TICKS(1000)); continue;
        }
        ESP_LOGI(TAG, "Modbus TCP server on port %d, unit %u, max clients %u", PORT, (unsigned)s_unit, s_max_clients);
        for (;;) {
            struct sockaddr_storage src; socklen_t sl = sizeof(src);
            const int cfd = accept(lfd, (struct sockaddr *)&src, &sl);
            if (cfd < 0) { ESP_LOGW(TAG, "accept errno %d", errno); break; }
            if (__atomic_load_n(&s_clients, __ATOMIC_SEQ_CST) >= s_max_clients) {          /* NFR-023: connection limit */
                ESP_LOGW(TAG, "connection refused: %u clients", s_clients);
                close(cfd); continue;
            }
            client_arg_t *ca = malloc(sizeof(*ca));
            if (!ca) { close(cfd); continue; }
            ca->fd = cfd; ca->conn = ++s_next_conn;
            if (xTaskCreate(client_task, "fw_client", 4096, ca, 6, NULL) != pdPASS) { close(cfd); free(ca); }
        }
        close(lfd);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

esp_err_t fw_mb_tcp_start(void)
{
    uint16_t net[FW_NET_WORDS];
    fw_motion_net_words(net);
    s_unit = (uint8_t)net[FW_N_UNIT];
    const fw_dev_t *d = fw_motion_dev();
    s_max_clients = d->cfg.sys[FW_S_MAX_CLIENTS];            /* read once: applied after reboot */
    return xTaskCreate(server_task, "fw_tcp", 5120, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
