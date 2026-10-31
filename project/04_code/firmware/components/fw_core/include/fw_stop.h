/* fw_core / fw_stop: per-axis STOP latch and START edge state machines (D-058, baseline v0.4 sec.3).
 * Source: project/04_code/test-fw/components/tfw_logic tfw_es_* / tfw_start_* (reviewed, M-01 M-04 N-03 applied).
 * Rules implemented here:
 *  - STOP (NC contact, input active = contact open) trips a per-axis latch (ISR calls fw_es_trip, always_inline so
 *    that it is compiled into the IRAM ISR - review M-01).
 *  - A STOP that is already active at boot latches at once (review M-04).
 *  - The latch is released ONLY after the input has been inactive for FW_ES_RELEASE_STABLE_MS and only by a START
 *    edge (no reset command). A START edge while STOP is active/latched-and-not-releasable is consumed and ignored. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "fw_profile.h"

#define FW_ES_RELEASE_STABLE_MS 10u   /* [assumed] input must stay inactive this long before a release */
#define FW_ES_REFUSED           0x10u /* fw_es_release() result: refused */

typedef struct {
    volatile uint8_t latched;
    uint16_t trip_count;
    uint32_t snap;                 /* pulse count captured at the trip */
    uint32_t stable_ms;            /* how long the input has been inactive */
} fw_es_t;

void fw_es_init(fw_es_t *e);
/* ISR-callable: always_inline so it is part of the IRAM ISR and never lives in flash */
FW_INLINE void fw_es_trip(fw_es_t *e, uint32_t count_snapshot)
{
    e->snap = count_snapshot;
    e->latched = 1;
    e->stable_ms = 0;
    e->trip_count++;
}
FW_INLINE bool fw_es_blocking(const fw_es_t *e, bool input_active) { return e->latched != 0 || input_active; }
void     fw_es_boot_check(fw_es_t *e, bool input_active);          /* latch at once if active at boot */
uint16_t fw_es_state(const fw_es_t *e, bool input_active);         /* 0 normal, 1 input active, 2 released-latched */
uint16_t fw_es_release(fw_es_t *e, bool input_active);            /* 0 ok, FW_ES_REFUSED */
void     fw_es_input_sample(fw_es_t *e, bool input_active, uint32_t dt_ms);

typedef struct { bool stable; uint32_t cand_ms; } fw_start_t;
void fw_start_init(fw_start_t *s, bool initial_active);            /* active at boot is NOT an edge */
/* call every dt_ms; true exactly once per inactive->active transition that stayed stable for debounce_ms, unless
 * stop_active at that moment (then the edge is consumed and ignored) */
bool fw_start_sample(fw_start_t *s, bool raw_active, bool stop_active, uint32_t dt_ms, uint32_t debounce_ms);

/* One combined 1 ms step for one axis, as run by the HAL watch task:
 * returns true when the START edge must start a move (after releasing the latch if it was latched). */
bool fw_stopstart_step(fw_es_t *es, fw_start_t *st, bool stop_input, bool start_raw, uint32_t debounce_ms,
                       uint16_t *start_edges, uint16_t *start_ignored);
