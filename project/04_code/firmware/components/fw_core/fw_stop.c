#include "fw_stop.h"

void fw_es_init(fw_es_t *e) { e->latched = 0; e->trip_count = 0; e->snap = 0; e->stable_ms = 0; }
void fw_es_boot_check(fw_es_t *e, bool input_active)
{
    if (input_active && !e->latched) fw_es_trip(e, 0u);
}
uint16_t fw_es_state(const fw_es_t *e, bool input_active)
{
    if (input_active) return 1;
    return e->latched ? 2 : 0;
}
void fw_es_input_sample(fw_es_t *e, bool input_active, uint32_t dt_ms)
{
    if (input_active) e->stable_ms = 0;
    else if (e->stable_ms < 0xFFFFFFF0u) e->stable_ms += dt_ms;
}
uint16_t fw_es_release(fw_es_t *e, bool input_active)
{
    if (input_active || e->stable_ms < FW_ES_RELEASE_STABLE_MS) return FW_ES_REFUSED;
    e->latched = 0;
    return 0;
}

void fw_start_init(fw_start_t *s, bool initial_active) { s->stable = initial_active; s->cand_ms = 0; }
bool fw_start_sample(fw_start_t *s, bool raw_active, bool stop_active, uint32_t dt_ms, uint32_t debounce_ms)
{
    if (raw_active == s->stable) { s->cand_ms = 0; return false; }
    if (s->cand_ms < 0xFFFFFFF0u) s->cand_ms += dt_ms;
    if (s->cand_ms < debounce_ms) return false;
    s->stable = raw_active;
    s->cand_ms = 0;
    return raw_active && !stop_active;
}

bool fw_stopstart_step(fw_es_t *es, fw_start_t *st, bool stop_input, bool start_raw, uint32_t debounce_ms,
                       uint16_t *start_edges, uint16_t *start_ignored)
{
    fw_es_input_sample(es, stop_input, 1);
    if (!fw_start_sample(st, start_raw, stop_input, 1, debounce_ms)) return false;
    if (start_edges) (*start_edges)++;
    if (es->latched && fw_es_release(es, stop_input) != 0) {
        if (start_ignored) (*start_ignored)++;
        return false;
    }
    return true;
}
