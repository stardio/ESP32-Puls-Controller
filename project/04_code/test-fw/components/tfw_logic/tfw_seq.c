#include "tfw_logic.h"

static uint32_t timeout_ms(uint32_t speed, uint32_t count, uint32_t acc, uint32_t dec)
{
    if (speed == 0) speed = 1;
    uint64_t t = 2u * ((2u * (uint64_t)count * 1000u) / speed + acc + dec) + 2000u;
    return t > 0xFFFFFFF0u ? 0xFFFFFFF0u : (uint32_t)t;
}

uint32_t tfw_seq_run(const tfw_seq_hw_t *hw, tfw_regwin_t *w, volatile tfw_results_t *res, uint16_t cmd)
{
    const uint16_t mask = w->axis_mask;
    const uint32_t freq = w->freq, base_count = w->count, acc0 = w->accel_ms, dec0 = w->decel_ms;
    uint32_t rng = w->seed;
    const bool randomise = (cmd == TFW_CMD_RUN_SEQ) && (w->seed != 0);
    const uint32_t iterations = (cmd == TFW_CMD_RUN_SEQ) ? w->repeat : 1u;
    uint32_t done = 0;

    w->status = TFW_ST_RUNNING;
    for (uint32_t it = 0; it < iterations; ++it) {
        uint32_t target[2] = {0, 0}, tmo = 0;
        bool started[2] = {false, false};
        /* start both axes back-to-back */
        for (uint8_t ax = 0; ax < 2; ++ax) {
            if (!(mask & (1u << ax))) continue;
            uint32_t speed = freq, count = base_count, acc = acc0, dec = dec0;
            if (randomise) {
                count = tfw_rand_distance(&rng);
                tfw_rand_motion(&rng, freq, &speed, &acc, &dec);
            }
            target[ax] = count;
            int rc = (cmd == TFW_CMD_RUN_FIXED) ? hw->fixed(hw->ctx, ax, speed, count)
                                                : hw->move(hw->ctx, ax, speed, count, acc, dec);
            if (rc == 0) {
                started[ax] = true;
                uint32_t t = timeout_ms(speed, count, acc, dec);
                if (t > tmo) tmo = t;
            } else {
                res->err[ax] = res->err[ax] + 1u;   /* start failure counts as an error */
            }
            res->last_target[ax] = count;
        }
        /* wait for all */
        const uint32_t t0 = hw->now_ms(hw->ctx);
        bool abort = false, timed_out = false;
        for (;;) {
            bool any = false;
            for (uint8_t ax = 0; ax < 2; ++ax) if (started[ax] && hw->busy(hw->ctx, ax)) any = true;
            if (!any) break;
            if (hw->aborted(hw->ctx)) { abort = true; break; }
            if ((uint32_t)(hw->now_ms(hw->ctx) - t0) > tmo) { timed_out = true; break; }
            hw->delay_ms(hw->ctx, 1);
        }
        if (abort || timed_out) for (uint8_t ax = 0; ax < 2; ++ax) if (started[ax]) hw->stop(hw->ctx, ax);
        /* judge */
        for (uint8_t ax = 0; ax < 2; ++ax) {
            if (!started[ax]) continue;
            uint32_t pcnt = hw->count(hw->ctx, ax);
            tfw_judge_t j = {0, 0};
            tfw_judge_move(&j, target[ax], pcnt);
            res->err[ax] = res->err[ax] + j.err;
            res->overshoot[ax] = res->overshoot[ax] + j.overshoot;
            res->last_pcnt[ax] = pcnt;
        }
        if (abort) { w->status = TFW_ST_IDLE; return done; }
        done++;
        res->seq_done = res->seq_done + 1u;
    }
    w->status = TFW_ST_DONE;
    return done;
}
