"""X/Y linear and circular interpolation path maths (D-089). Mirrors firmware fw_core/fw_interp.c so that the PC
preview and the simulator follow the same path and timing as the controller. Units: pulses, seconds."""
import math

LINE, ARC_CW, ARC_CCW = 1, 2, 3
E_RANGE, E_ABORT, E_TIMEOUT = 0x17, 0x20, 0x21
RADIUS_TOL = 2.0          # pulses: end point may miss the circle by this much (integer rounding)
TWO_PI = 2 * math.pi


class PlanError(ValueError):
    pass


def _turn(frm, to):
    d = math.fmod(to - frm, TWO_PI)
    if d < 0:
        d += TWO_PI                    # C fmod keeps the sign of the dividend; normalise the same way as the firmware
    if d <= 1e-12:
        d += TWO_PI
    return d


class Path:
    def __init__(self, mode, dx, dy, ci, cj, feed, accel_ms, decel_ms):
        if feed <= 0 or accel_ms <= 0 or decel_ms <= 0:
            raise PlanError("이송 속도·가감속 시간은 0보다 커야 합니다")
        self.mode, self.dx, self.dy, self.ci, self.cj = mode, float(dx), float(dy), float(ci), float(cj)
        self.r = self.a0 = self.sweep = self.ex = self.ey = 0.0
        if mode == LINE:
            self.len = math.hypot(self.dx, self.dy)
        elif mode in (ARC_CW, ARC_CCW):
            self.r = math.hypot(self.ci, self.cj)
            if self.r < 1.0:
                raise PlanError("원호 중심(I, J)이 시작점과 같습니다")
            ex, ey = self.dx - self.ci, self.dy - self.cj
            if abs(math.hypot(ex, ey) - self.r) > RADIUS_TOL:
                raise PlanError(f"끝점이 원 위에 있지 않습니다 (반지름 {self.r:.1f}, 끝점 거리 {math.hypot(ex, ey):.1f}, "
                                f"허용 {RADIUS_TOL:g} pulse)")
            self.a0 = math.atan2(-self.cj, -self.ci)
            a1 = math.atan2(ey, ex)
            self.sweep = _turn(self.a0, a1) if mode == ARC_CCW else -_turn(a1, self.a0)
            self.len = self.r * abs(self.sweep)
            a_end = self.a0 + self.sweep           # programmed end - circle end, spread over the path (review M-03)
            self.ex = self.dx - (self.ci + self.r * math.cos(a_end))
            self.ey = self.dy - (self.cj + self.r * math.sin(a_end))
        else:
            raise PlanError("보간 방식 오류")
        if self.len < 1.0:
            raise PlanError("이동 거리가 0입니다")
        v = float(feed)
        self.acc, self.dec = v / (accel_ms / 1000.0), v / (decel_ms / 1000.0)
        d_acc, d_dec = v * v / (2 * self.acc), v * v / (2 * self.dec)
        self.vpk = v if d_acc + d_dec <= self.len else math.sqrt(2 * self.len * self.acc * self.dec / (self.acc + self.dec))
        self.t1 = self.vpk / self.acc
        cruise = self.len - self.vpk ** 2 / (2 * self.acc) - self.vpk ** 2 / (2 * self.dec)
        self.t2 = self.t1 + (cruise / self.vpk if cruise > 0 else 0.0)
        self.T = self.t2 + self.vpk / self.dec
        self.stopping, self.ts, self.ss, self.vs = False, 0.0, 0.0, 0.0

    def _plan_s(self, t):
        if t <= 0:
            return 0.0, 0.0
        if t < self.t1:
            return 0.5 * self.acc * t * t, self.acc * t
        s1 = 0.5 * self.acc * self.t1 ** 2
        if t < self.t2:
            return s1 + self.vpk * (t - self.t1), self.vpk
        s2 = s1 + self.vpk * (self.t2 - self.t1)
        if t < self.T:
            u = t - self.t2
            return s2 + self.vpk * u - 0.5 * self.dec * u * u, self.vpk - self.dec * u
        return self.len, 0.0

    def s(self, t):
        """(path position, path speed) at time t."""
        if not self.stopping or t <= self.ts:
            return self._plan_s(t)
        u, tu = t - self.ts, self.vs / self.dec
        if u >= tu:
            return self.ss + self.vs * tu - 0.5 * self.dec * tu * tu, 0.0
        return self.ss + self.vs * u - 0.5 * self.dec * u * u, self.vs - self.dec * u

    def xy(self, s):
        if self.mode == LINE:
            k = s / self.len
            return self.dx * k, self.dy * k
        a, k = self.a0 + (s if self.sweep >= 0 else -s) / self.r, s / self.len
        return self.ci + self.r * math.cos(a) + self.ex * k, self.cj + self.r * math.sin(a) + self.ey * k

    def stop(self, t):
        if self.stopping:
            return
        s, v = self._plan_s(t)
        if t >= self.t2:
            return
        self.stopping, self.ts, self.ss, self.vs = True, t, s, v
        self.T = t + v / self.dec

    def end_s(self):
        return self.s(self.T + 1.0)[0]

    def axis_peak(self):
        if self.mode == LINE:
            return self.vpk * abs(self.dx) / self.len, self.vpk * abs(self.dy) / self.len
        return self.vpk, self.vpk

    def samples(self, n=400):
        """[(t, x, y, feed)] evenly spaced in time, including both ends (relative to the start point)."""
        out = []
        for i in range(n + 1):
            t = self.T * i / n
            s, v = self.s(t)
            x, y = self.xy(s)
            out.append((t, x, y, v))
        out[-1] = (self.T, self.dx, self.dy, 0.0) if not self.stopping else out[-1]
        return out
