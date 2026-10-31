/* fw_hal: hardware abstraction (DC-14). Everything the product logic needs from the chip is declared here.
 * Implementations: port_esp32p4/ (ESP-IDF 5.5.3, target esp32p4). The host tests link a fake (tests/firmware/fake_hal.c).
 * Conventions: int results are 0 = ok. Functions marked [ISR-safe] may be called from interrupt context. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- pulse engine (2 axes: MCPWM timer/operator/generator per axis + PCNT unit counting the output pin) ---- */
enum { FW_PE_OK = 0, FW_PE_BAD_ARG = 1, FW_PE_STOP_BLOCK = 2, FW_PE_BUSY = 3, FW_PE_FAIL = 4 };
int      fw_hal_pulse_init(uint8_t axis_count, const uint32_t res_hz[2]);   /* creates the axes; output stays forbidden */
bool     fw_hal_pulse_ready(uint8_t axis);
/* finite move: first_hz = start frequency, target_pulses >= 1. Refused (FW_PE_STOP_BLOCK) while the axis STOP is
 * latched/active/not initialised. */
int      fw_hal_pulse_start(uint8_t axis, uint32_t first_hz, uint32_t target_pulses);
int      fw_hal_pulse_set_freq(uint8_t axis, uint32_t hz);   /* FW_PE_BAD_ARG: not representable, previous period kept */
void     fw_hal_pulse_stop(uint8_t axis);                    /* [ISR-safe] force LOW + timer stop */
bool     fw_hal_pulse_finished(uint8_t axis);                /* target reached (set by the PCNT ISR), not yet ended */
void     fw_hal_pulse_end(uint8_t axis);                     /* finalise after stop/finish: timer, PCNT, watch points; idempotent */
bool     fw_hal_pulse_active(uint8_t axis);
uint32_t fw_hal_pulse_count(uint8_t axis);                   /* pulses emitted since fw_hal_pulse_start */
void     fw_hal_dir_set(uint8_t axis, bool level_high);

/* ---- shared pulse output enable (GPIO33 -> AM26C31 /G). Must be LOW from the first instruction of boot. ---- */
void     fw_hal_oe_early_forbid(void);                       /* first call in app_main */
void     fw_hal_oe_forbid(void);
void     fw_hal_oe_allow(void);
bool     fw_hal_oe_allowed(void);

/* ---- digital I/O ---- */
void     fw_hal_servo_set(uint8_t axis, bool on);            /* DO1 (X) / DO2 (Y) */
void     fw_hal_user_do_set(uint16_t mask);                  /* b0 = DO3, b1 = DO4 */
uint16_t fw_hal_do_state(void);                              /* b0..b3 = DO1..DO4 as driven */
uint16_t fw_hal_di_raw(void);                                /* b0 X START, b1 X STOP, b2/b3 X spare, b4 Y START, b5 Y STOP, b6/b7 Y spare; 1 = active */

/* ---- STOP (NC contact, per axis) and START (edge) ---- */
int      fw_hal_io_init(uint8_t axis_count);                 /* GPIO ISR + MCPWM fault + START polling task */
bool     fw_hal_io_ready(void);
bool     fw_hal_stop_input(uint8_t axis);                    /* input active right now */
bool     fw_hal_stop_latched(uint8_t axis);
bool     fw_hal_stop_blocking(uint8_t axis);                 /* latched || active || io not initialised */
uint16_t fw_hal_stop_state(uint8_t axis);                    /* 0 normal, 1 input active, 2 released-latched */
uint16_t fw_hal_stop_trips(uint8_t axis);
/* called from the watch task after an accepted START edge (latch already released); must not block */
void     fw_hal_set_start_cb(void (*cb)(uint8_t axis));

/* ---- system ---- */
uint32_t fw_hal_ms(void);                                    /* monotonic ms */
void     fw_hal_delay_us(uint32_t us);
uint16_t fw_hal_reset_reason(void);                          /* FW_RST_* of fw_params.h */
int      fw_hal_wdt_init(void);                              /* task watchdog, panic = reset */
void     fw_hal_wdt_add_self(void);
void     fw_hal_wdt_feed(void);
void     fw_hal_reboot(void);                                /* does not return */
void     fw_hal_mac(uint8_t mac[6]);

/* ---- non-volatile storage (one blob) ---- */
enum { FW_ST_OK = 0, FW_ST_NOT_FOUND = 1, FW_ST_BAD_SIZE = 2, FW_ST_ERR = -1 };
int      fw_hal_store_init(void);
int      fw_hal_store_read(void *buf, size_t *len);          /* FW_ST_OK / FW_ST_NOT_FOUND / FW_ST_ERR */
int      fw_hal_store_write(const void *buf, size_t len);    /* FW_ST_OK / FW_ST_ERR */

#ifdef __cplusplus
}
#endif
