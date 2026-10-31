/* fw_motion: boot sequence, device model owner, motion task, deferred NVS save, comm-loss stop.
 * Transports (fw_modbus) talk to it only through fw_motion_process_pdu() and the small setters below. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "fw_regmap.h"

typedef struct { uint8_t axis_count; bool model_rtu; uint32_t fw_version; } fw_motion_cfg_t;

/* Boot sequence (GPIO33 is already LOW: fw_hal_oe_early_forbid() ran first in app_main).
 * Returns 0 when pulses are allowed, non-zero when operation is prohibited (registers stay readable). */
int  fw_motion_start(const fw_motion_cfg_t *cfg);

const fw_dev_t *fw_motion_dev(void);                                  /* read-only view; hold fw_motion_lock for consistency */
void fw_motion_lock(void);
void fw_motion_unlock(void);
uint16_t fw_motion_comm_mode(void);                                   /* FW_MODE_TCP / FW_MODE_RTU actually used this boot */
void fw_motion_net_words(uint16_t net[FW_NET_WORDS]);                 /* copy of the network settings words */
void fw_motion_set_link(bool up);
void fw_motion_set_clients(unsigned n);
void fw_motion_set_mac(const uint8_t mac[6]);
/* One request PDU from connection `conn` (any int; -1 for RTU). Locks, stamps time, processes. */
size_t fw_motion_process_pdu(int32_t conn, const uint8_t *req, size_t len, uint8_t *resp);
void fw_motion_conn_closed(int32_t conn);
