/* fw_core / fw_interp: X/Y linear and circular interpolation path maths (D-089). Hardware independent, host-testable.
 * Units: pulses and seconds. The path starts at (0,0); the caller adds the start position. The feed (path speed)
 * follows a trapezoid: accel -> cruise -> decel. The PC tool (svtool/interp.py) mirrors this file for the preview. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { FW_IP_NONE = 0, FW_IP_LINE = 1, FW_IP_ARC_CW = 2, FW_IP_ARC_CCW = 3 };
/* plan result (also written to ip_result): 0 ok; others reuse the axis cmd_result codes where they fit */
enum { FW_IP_OK = 0, FW_IP_E_RANGE = 0x17 /* mode, feed, zero length, end not on the circle */, FW_IP_E_ABORT = 0x20,
       FW_IP_E_TIMEOUT = 0x21 };
#define FW_IP_RADIUS_TOL 2.0          /* pulses: end point may miss the circle by this much (integer rounding) */

typedef struct {
    uint8_t mode;
    double dx, dy;                    /* end point relative to the start */
    double ci, cj;                    /* arc centre relative to the start */
    double r, a0, sweep;              /* arc: radius, start angle, signed sweep (rad, CCW positive) */
    double ex, ey;                    /* arc: programmed end - circle end, spread over the path (review M-03) */
    double len;                       /* path length */
    double acc, dec;                  /* path acceleration / deceleration (pulses/s^2) */
    double vpk, t1, t2, T;            /* peak feed, end of accel, start of decel, end time */
    bool   stopping;                  /* fw_ip_stop() was called: decel from (ts, ss, vs) to a stop at T */
    double ts, ss, vs;
} fw_ip_t;

/* plan a move. feed in pulses/s along the path, accel/decel in ms from 0 to feed. 0 or FW_IP_E_RANGE */
int    fw_ip_plan(fw_ip_t *p, uint8_t mode, int32_t dx, int32_t dy, int32_t ci, int32_t cj, uint32_t feed,
                  uint16_t accel_ms, uint16_t decel_ms);
double fw_ip_s(const fw_ip_t *p, double t, double *v);      /* path position at time t (clamped), *v = path speed */
void   fw_ip_xy(const fw_ip_t *p, double s, double *x, double *y);   /* point at path position s */
void   fw_ip_stop(fw_ip_t *p, double t);                    /* decelerate along the path from time t (idempotent) */
double fw_ip_end_s(const fw_ip_t *p);                       /* path position where the move ends (len, or the stop) */
/* highest axis speed the path asks for at feed vpk: linear = vpk * |dx| / len, arc = vpk (both axes) */
void   fw_ip_axis_peak(const fw_ip_t *p, double *vx, double *vy);
