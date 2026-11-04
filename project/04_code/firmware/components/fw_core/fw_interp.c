#include "fw_interp.h"
#include <math.h>
#include <string.h>

#define TWO_PI 6.283185307179586

/* angle difference normalised to (0, 2pi]; 0 (end == start) means a full turn */
static double turn(double from, double to)
{
    double d = fmod(to - from, TWO_PI);
    if (d <= 1e-12) d += TWO_PI;
    return d;
}

int fw_ip_plan(fw_ip_t *p, uint8_t mode, int32_t dx, int32_t dy, int32_t ci, int32_t cj, uint32_t feed,
               uint16_t accel_ms, uint16_t decel_ms)
{
    memset(p, 0, sizeof(*p));
    if (feed == 0 || accel_ms == 0 || decel_ms == 0) return FW_IP_E_RANGE;
    p->mode = mode; p->dx = dx; p->dy = dy; p->ci = ci; p->cj = cj;
    if (mode == FW_IP_LINE) {
        p->len = sqrt(p->dx * p->dx + p->dy * p->dy);
    } else if (mode == FW_IP_ARC_CW || mode == FW_IP_ARC_CCW) {
        p->r = sqrt(p->ci * p->ci + p->cj * p->cj);
        if (p->r < 1.0) return FW_IP_E_RANGE;
        const double ex = p->dx - p->ci, ey = p->dy - p->cj;
        if (fabs(sqrt(ex * ex + ey * ey) - p->r) > FW_IP_RADIUS_TOL) return FW_IP_E_RANGE;   /* end not on the circle */
        p->a0 = atan2(-p->cj, -p->ci);
        const double a1 = atan2(ey, ex);
        p->sweep = mode == FW_IP_ARC_CCW ? turn(p->a0, a1) : -turn(a1, p->a0);
        p->len = p->r * fabs(p->sweep);
        p->ex = p->dx - (p->ci + p->r * cos(p->a0 + p->sweep));     /* <= tolerance: the path ends on the programmed point */
        p->ey = p->dy - (p->cj + p->r * sin(p->a0 + p->sweep));
    } else {
        return FW_IP_E_RANGE;
    }
    if (p->len < 1.0) return FW_IP_E_RANGE;

    const double v = feed;
    p->acc = v / (accel_ms / 1000.0);
    p->dec = v / (decel_ms / 1000.0);
    const double d_acc = v * v / (2.0 * p->acc), d_dec = v * v / (2.0 * p->dec);
    p->vpk = (d_acc + d_dec <= p->len) ? v : sqrt(2.0 * p->len * p->acc * p->dec / (p->acc + p->dec));   /* triangle */
    p->t1 = p->vpk / p->acc;
    const double cruise = p->len - p->vpk * p->vpk / (2.0 * p->acc) - p->vpk * p->vpk / (2.0 * p->dec);
    p->t2 = p->t1 + (cruise > 0.0 ? cruise / p->vpk : 0.0);
    p->T = p->t2 + p->vpk / p->dec;
    return FW_IP_OK;
}

/* the planned trapezoid, ignoring a stop request */
static double plan_s(const fw_ip_t *p, double t, double *v)
{
    if (t <= 0.0) { *v = 0.0; return 0.0; }
    if (t < p->t1) { *v = p->acc * t; return 0.5 * p->acc * t * t; }
    const double s1 = 0.5 * p->acc * p->t1 * p->t1;
    if (t < p->t2) { *v = p->vpk; return s1 + p->vpk * (t - p->t1); }
    const double s2 = s1 + p->vpk * (p->t2 - p->t1);
    if (t < p->T) { const double u = t - p->t2; *v = p->vpk - p->dec * u; return s2 + p->vpk * u - 0.5 * p->dec * u * u; }
    *v = 0.0;
    return p->len;
}

double fw_ip_s(const fw_ip_t *p, double t, double *v)
{
    double dummy;
    if (!v) v = &dummy;
    if (!p->stopping || t <= p->ts) return plan_s(p, t, v);
    const double u = t - p->ts, tu = p->vs / p->dec;
    if (u >= tu) { *v = 0.0; return p->ss + p->vs * tu - 0.5 * p->dec * tu * tu; }
    *v = p->vs - p->dec * u;
    return p->ss + p->vs * u - 0.5 * p->dec * u * u;
}

void fw_ip_xy(const fw_ip_t *p, double s, double *x, double *y)
{
    if (p->mode == FW_IP_LINE) {
        const double k = p->len > 0.0 ? s / p->len : 0.0;
        *x = p->dx * k; *y = p->dy * k;
        return;
    }
    const double a = p->a0 + (p->sweep >= 0.0 ? s : -s) / p->r, k = s / p->len;
    *x = p->ci + p->r * cos(a) + p->ex * k;
    *y = p->cj + p->r * sin(a) + p->ey * k;
}

void fw_ip_stop(fw_ip_t *p, double t)
{
    if (p->stopping) return;
    double v;
    const double s = plan_s(p, t, &v);
    if (t >= p->t2) return;                       /* already decelerating to the end: that is the shortest stop */
    p->stopping = true; p->ts = t; p->ss = s; p->vs = v;
    p->T = t + v / p->dec;
}

double fw_ip_end_s(const fw_ip_t *p)
{
    double v;
    return fw_ip_s(p, p->T + 1.0, &v);
}

void fw_ip_axis_peak(const fw_ip_t *p, double *vx, double *vy)
{
    if (p->mode == FW_IP_LINE && p->len > 0.0) { *vx = p->vpk * fabs(p->dx) / p->len; *vy = p->vpk * fabs(p->dy) / p->len; }
    else { *vx = p->vpk; *vy = p->vpk; }
}
