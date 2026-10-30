/* Modbus PDU engine. Request handling is adapted from servo1axis process_request() [E2]
 * (FC01/02/03/05/06/15/16, exception codes, legacy window rules). Differences:
 *  - no NVS call in here (risk 3): writes only mark legacy->dirty
 *  - test window 0x7000..0x70FF is dispatched to tfw_regwin */
#include "tfw_logic.h"
#include <string.h>

static uint16_t be16(const uint8_t *d) { return (uint16_t)((d[0] << 8) | d[1]); }
static void put16(uint8_t *d, uint16_t v) { d[0] = (uint8_t)(v >> 8); d[1] = (uint8_t)v; }
static size_t exc(uint8_t fc, uint8_t code, uint8_t *r) { r[0] = fc | 0x80; r[1] = code; return 2; }

uint32_t tfw_legacy_u32(const tfw_legacy_t *l, unsigned low) { return l->hr[low] | ((uint32_t)l->hr[low + 1] << 16); }
void tfw_legacy_store_u32(tfw_legacy_t *l, unsigned low, uint32_t v) { l->hr[low] = (uint16_t)v; l->hr[low + 1] = (uint16_t)(v >> 16); }

void tfw_legacy_init(tfw_legacy_t *l)
{
    memset(l, 0, sizeof(*l));
    l->coils = 1u << TFW_COIL_DIR_FWD;
    tfw_legacy_store_u32(l, TFW_HR_POS, 5000);
    tfw_legacy_store_u32(l, TFW_HR_SPEED, 10000);
    tfw_legacy_store_u32(l, TFW_HR_START_SPD, tfw_start_speed(10000));
    tfw_legacy_store_u32(l, TFW_HR_ACCEL, 400);
    tfw_legacy_store_u32(l, TFW_HR_DECEL, 400);
}

static uint32_t u32_of(const uint16_t *hr, unsigned low) { return hr[low] | ((uint32_t)hr[low + 1] << 16); }

bool tfw_legacy_valid(const uint16_t *hr)
{
    uint32_t pos = u32_of(hr, TFW_HR_POS), spd = u32_of(hr, TFW_HR_SPEED);
    uint32_t acc = u32_of(hr, TFW_HR_ACCEL), dec = u32_of(hr, TFW_HR_DECEL);
    return pos >= 1 && spd >= 100 && spd <= 1000000u && acc >= 10 && acc <= 60000 && dec >= 10 && dec <= 60000;
}

static bool legacy_writable(unsigned a) { return a <= 3 || (a >= TFW_HR_ACCEL && a <= TFW_HR_DECEL + 1); }

static void auto_start(uint16_t *hr)
{
    uint32_t s = tfw_start_speed(u32_of(hr, TFW_HR_SPEED));
    hr[TFW_HR_START_SPD] = (uint16_t)s; hr[TFW_HR_START_SPD + 1] = (uint16_t)(s >> 16);
}

void tfw_legacy_refresh(tfw_legacy_t *l, bool link, unsigned clients, bool moving)
{
    uint16_t st = 0;
    if (link) st |= 1u << 0;
    if (clients) st |= 1u << 1;
    if (l->coils & (1u << TFW_COIL_SERVO)) st |= 1u << 2;
    if (moving) st |= 1u << 3;
    if (l->hr[TFW_HR_RESULT] == 1) st |= 1u << 4;
    if (l->hr[TFW_HR_FAULT]) st |= 1u << 5;
    l->hr[TFW_HR_STATUS] = st;
    l->hr[TFW_HR_CLIENTS] = (uint16_t)(clients > 0xFFFFu ? 0xFFFFu : clients);
    l->hr[TFW_HR_LINK] = link ? 1 : 0;
}

uint8_t tfw_legacy_di(const tfw_legacy_t *l, bool link, unsigned clients, bool moving)
{
    uint8_t v = 0;
    if (moving) v |= 1u << 0;
    if (link) v |= 1u << 1;
    if (clients) v |= 1u << 2;
    if (l->hr[TFW_HR_RESULT] == 1) v |= 1u << 3;
    if (l->hr[TFW_HR_FAULT]) v |= 1u << 4;
    return v;
}

size_t tfw_mb_process(tfw_mb_ctx_t *c, const uint8_t *req, size_t len, uint8_t *resp)
{
    if (!len) return 0;
    const uint8_t fc = req[0];
    tfw_legacy_t *l = c->legacy;

    if (fc == 1 || fc == 2) {
        if (len != 5) return exc(fc, 3, resp);
        uint16_t a = be16(req + 1), n = be16(req + 3);
        if (!n || n > TFW_COIL_COUNT || a + n > TFW_COIL_COUNT) return exc(fc, 2, resp);
        unsigned bc = (n + 7u) / 8u;
        resp[0] = fc; resp[1] = (uint8_t)bc; memset(resp + 2, 0, bc);
        tfw_legacy_refresh(l, c->link, c->clients, c->moving);
        uint16_t vals = (fc == 2) ? tfw_legacy_di(l, c->link, c->clients, c->moving) : l->coils;
        for (unsigned i = 0; i < n; ++i) if (vals & (1u << (a + i))) resp[2 + i / 8] |= (uint8_t)(1u << (i % 8));
        return 2 + bc;
    }
    if (fc == 5) {
        if (len != 5) return exc(fc, 3, resp);
        uint16_t a = be16(req + 1), v = be16(req + 3);
        if (a >= TFW_COIL_COUNT) return exc(fc, 2, resp);
        if (v != 0 && v != 0xFF00) return exc(fc, 3, resp);
        if (v) l->coils |= (uint16_t)(1u << a); else l->coils &= (uint16_t)~(1u << a);
        if (a == TFW_COIL_DIR_FWD) l->dirty = true;
        memcpy(resp, req, 5);
        return 5;
    }
    if (fc == 15) {
        if (len < 6) return exc(fc, 3, resp);
        uint16_t a = be16(req + 1), n = be16(req + 3); uint8_t bc = req[5];
        if (!n || n > TFW_COIL_COUNT || a + n > TFW_COIL_COUNT || bc != (n + 7u) / 8u || len != (size_t)(6 + bc))
            return exc(fc, 3, resp);
        for (unsigned i = 0; i < n; ++i) {
            if (req[6 + i / 8] & (1u << (i % 8))) l->coils |= (uint16_t)(1u << (a + i));
            else l->coils &= (uint16_t)~(1u << (a + i));
        }
        resp[0] = fc; put16(resp + 1, a); put16(resp + 3, n);
        return 5;
    }
    if (fc == 3) {
        if (len != 5) return exc(fc, 3, resp);
        uint16_t a = be16(req + 1), n = be16(req + 3);
        if (!n || n > 125) return exc(fc, 2, resp);
        uint16_t tmp[125];
        if (a >= TFW_WIN_BASE) {
            tfw_results_t r;
            memset(&r, 0, sizeof(r));
            if (c->get_results) c->get_results(c->user, &r);
            if (tfw_regwin_read(c->win, &r, a, n, tmp) != 0) return exc(fc, 2, resp);
        } else {
            if (a + n > TFW_HR_LEGACY_COUNT) return exc(fc, 2, resp);
            tfw_legacy_refresh(l, c->link, c->clients, c->moving);
            memcpy(tmp, l->hr + a, n * sizeof(uint16_t));
        }
        resp[0] = fc; resp[1] = (uint8_t)(n * 2);
        for (unsigned i = 0; i < n; ++i) put16(resp + 2 + i * 2, tmp[i]);
        return 2 + n * 2u;
    }
    if (fc == 6 || fc == 16) {
        uint16_t a, n; uint16_t one[1];
        if (fc == 6) {
            if (len != 5) return exc(fc, 3, resp);
            a = be16(req + 1); one[0] = be16(req + 3); n = 1;
        } else {
            if (len < 6) return exc(fc, 3, resp);
            a = be16(req + 1); n = be16(req + 3); uint8_t bc = req[5];
            if (!n || n > 123 || bc != n * 2 || len != (size_t)(6 + bc)) return exc(fc, 3, resp);
        }
        uint16_t vals[123];
        if (fc == 6) vals[0] = one[0];
        else for (unsigned i = 0; i < n; ++i) vals[i] = be16(req + 6 + i * 2);

        if (a >= TFW_WIN_BASE) {
            int rc = tfw_regwin_write(c->win, a, n, vals, &c->fx);
            if (rc) return exc(fc, (uint8_t)rc, resp);
        } else {
            if ((uint32_t)a + n > TFW_HR_LEGACY_COUNT) return exc(fc, 2, resp);
            for (unsigned i = 0; i < n; ++i) if (!legacy_writable(a + i)) return exc(fc, 2, resp);
            uint16_t cand[TFW_HR_LEGACY_COUNT];
            memcpy(cand, l->hr, sizeof(cand));
            for (unsigned i = 0; i < n; ++i) cand[a + i] = vals[i];
            auto_start(cand);
            if (!tfw_legacy_valid(cand)) return exc(fc, 3, resp);
            memcpy(l->hr, cand, sizeof(cand));
            l->dirty = true;     /* risk 3: NVS save is the caller's explicit, optional decision */
        }
        if (fc == 6) memcpy(resp, req, 5);
        else { resp[0] = fc; put16(resp + 1, a); put16(resp + 3, n); }
        return 5;
    }
    return exc(fc, 1, resp);
}
