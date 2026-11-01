"""Modbus TCP simulator of the controller register map (FR-038). Time based, no hardware.

Time is advanced lazily on every request (and by Sim.advance() in tests), so behaviour is deterministic with
an injected clock. HMI panels can connect to it for practice.
Simplifications [REC]: constant-speed motion (no ramp shape; STOP ramps down over decel time), position is
valid at start, HOME takes HOME_SECONDS, JOG keep-alive/connection-loss stop is not simulated.
"""
import socketserver
import struct
import sys
import threading
import time

from . import regmap as R, units

HOME_SECONDS = 1.0
TEST_HOLD_S = 0.5
FW_VERSION_SIM = (0 << 16) | (1 << 8) | 0     # 0.1.0 (simulator)


class SimException(Exception):
    def __init__(self, code):
        super().__init__(f"exception {code}")
        self.code = code


class Axis:
    def __init__(self, n):
        self.n = n
        self.pos = 0.0
        self.vel = 0.0
        self.mode = "idle"          # idle move jog test home stopping
        self.target = 0
        self.speed = 0
        self.dir = 1
        self.t_left = 0.0
        self.start_pos = 0.0
        self.pulses_done = 0
        self.servo_on = False
        self.alarm = 0
        self.alarm_count = 0
        self.done = False
        self.homed = False
        self.posinv = False
        self.sw = 0                 # b0 +sw limit hit, b1 -sw limit hit
        self.stop_cause = 0
        self.move_count = 0
        self.cmd_result = 0
        self.lim = {"+": False, "-": False}
        self.legacy_result = 0
        self.legacy_fault = 0

    @property
    def running(self):
        return self.mode != "idle"


class Sim:
    def __init__(self, axes=2, clock=time.monotonic):
        assert axes in (1, 2)
        self.axes_n = axes
        self.clock = clock
        self.lock = threading.RLock()
        self.fields = {}
        for f in R.all_fields(axes):
            for i in range(f.words):
                self.fields[f.addr + i] = (f, i)
        self.regs = {a: 0 for a in self.fields}
        self.reserved_legacy = set(range(0x0000, 0x0020)) - set(self.fields)
        self.axes = [Axis(n) for n in range(1, axes + 1)]
        self.coil_dir = True
        self.stop_active = False
        self.latch = False
        self.sys_alarm = 0
        self.unlock_until = -1.0
        self.unsaved = False
        self.eth_link = True
        self.clients = 0
        self.history = []
        self.wiring_override = {}
        self.uptime = 0.0
        self.last = clock()
        self.sys_result = 0
        self.reset_reason = 1
        self.nvm = None
        self._defaults_all()
        self.nvm = self._snapshot()
        self.unsaved = False
        self.regs[R.MODEL_ID] = axes
        self._set(R.AXIS_COUNT, axes)
        self._setw(R.FW_VERSION, 2, FW_VERSION_SIM)
        self._set(R.REGMAP_VER, R.REGMAP_VERSION)
        self._refresh()

    # ---- low level -------------------------------------------------------
    def _set(self, addr, v):
        self.regs[addr] = v & 0xFFFF

    def _setw(self, addr, n, v):
        for i in range(n):
            self.regs[addr + i] = (v >> (16 * i)) & 0xFFFF

    def _get(self, f):
        return R.decode(f.kind, [self.regs[f.addr + i] for i in range(f.words)])

    def _getn(self, name_or_addr):
        f = self.fields[name_or_addr][0]
        return self._get(f)

    def _fld(self, name):
        for f, i in self.fields.values():
            if f.name == name:
                return f
        raise KeyError(name)

    def p(self, n, off):
        """Read axis n param/data register at offset by address."""
        return self._get(self.fields[R.AXIS_BASE[n] + off][0])

    def _persist_fields(self):
        return [f for f, i in self.fields.values() if i == 0 and f.persist]

    def _snapshot(self):
        return {a: self.regs[a] for f in self._persist_fields() for a in range(f.addr, f.addr + f.words)}

    def _defaults_all(self, only_net=False):
        for f in self._persist_fields():
            if only_net and f.group != "network":
                continue
            for i, w in enumerate(R.encode(f.kind, f.default)):
                self.regs[f.addr + i] = w
        for n in range(1, self.axes_n + 1):
            if not only_net:
                for off, name in ((R.A_SPEED, None),):
                    self._set_default_runtime(n)
        self.coil_dir = True if not only_net else self.coil_dir
        if not only_net and hasattr(self, "axes"):
            for ax in self.axes:
                ax.mode = "idle"
        self.unsaved = True

    def _set_default_runtime(self, n):
        for f in R.axis_runtime_fields(n):
            if f.rw and f.default:
                for i, w in enumerate(R.encode(f.kind, f.default)):
                    self.regs[f.addr + i] = w

    def unlocked(self):
        return self.clock() <= self.unlock_until

    # ---- time ------------------------------------------------------------
    def advance(self, seconds):
        """Advance a fake clock (only if the injected clock exposes .advance)."""
        self.clock.advance(seconds)

    def update(self):
        with self.lock:
            now = self.clock()
            dt = max(0.0, now - self.last)
            t = self.last
            self.last = now
            self.uptime += dt
            while dt > 1e-9:          # sub-steps so that stop/ramp phases follow one another
                c = min(dt, 0.02)
                dt -= c
                t += c
                for ax in self.axes:
                    self._step(ax, c, t)
            self._refresh()

    def _finish(self, ax, cause, done):
        ax.mode = "idle"
        ax.vel = 0.0
        ax.stop_cause = cause
        ax.done = done
        if done:
            ax.move_count += 1
            ax.legacy_result = 1
        else:
            ax.legacy_result = 2
        ax.pulses_done = int(abs(ax.pos - ax.start_pos))

    def _begin_stop(self, ax, cause):
        if ax.mode == "stopping" or not ax.running:
            return
        decel = max(1, self.p(ax.n, R.A_DECEL)) / 1000.0
        ax.mode = "stopping"
        ax.t_left = decel
        ax._stop_cause = cause
        ax._stop_total = decel

    def _step(self, ax, dt, now):
        if ax.mode == "move":
            d = ax.target - ax.pos
            step = ax.speed * dt
            if abs(d) <= step:
                ax.pos = float(ax.target)
                self._finish(ax, 1, True)
            else:
                ax.pos += (1 if d > 0 else -1) * step
                ax.vel = (1 if d > 0 else -1) * ax.speed
        elif ax.mode in ("jog", "test"):
            if ax.mode == "test" and now > ax.t_left:
                self._begin_stop(ax, 4)
            else:
                if (ax.dir > 0 and ax.lim["+"]) or (ax.dir < 0 and ax.lim["-"]):
                    self._finish(ax, 0x0201 if ax.dir > 0 else 0x0202, False)
                    return
                ax.pos += ax.dir * ax.speed * dt
                ax.vel = ax.dir * ax.speed
                self._softlimit_clamp(ax)
        elif ax.mode == "stopping":
            adv = min(dt, ax.t_left)
            ax.pos += ax.vel * adv / 2
            ax.t_left -= dt
            if ax.t_left <= 0:
                self._finish(ax, ax._stop_cause, False)
        elif ax.mode == "home":
            ax.t_left -= dt
            if ax.t_left <= 0:
                ax.pos = float(self.p(ax.n, 0x117))
                ax.homed = True
                ax.posinv = False
                self._finish(ax, 1, True)

    def _softlimit_clamp(self, ax):
        if not self.p(ax.n, 0x11A) & 1:
            return
        hi, lo = self.p(ax.n, 0x11B), self.p(ax.n, 0x11D)
        if ax.pos > hi:
            ax.pos = float(hi)
            ax.sw = 1
            self._finish(ax, 0x0203, False)
        elif ax.pos < lo:
            ax.pos = float(lo)
            ax.sw = 2
            self._finish(ax, 0x0204, False)

    # ---- register refresh -------------------------------------------------
    def _unit_params(self, n):
        return (self.p(n, 0x140), self.p(n, 0x141), self.p(n, 0x143), self.p(n, 0x145), self.p(n, 0x146))

    def _refresh(self):
        any_alarm = False
        mask = 0
        for ax in self.axes:
            b = R.AXIS_BASE[ax.n]
            auto_on = self.p(ax.n, 0x129) == 0 and ax.running
            svon = ax.servo_on or auto_on
            if self.latch or self.stop_active:
                state = 7
            elif ax.alarm:
                state = 6
            elif ax.mode == "move":
                state = 2
            elif ax.mode in ("jog", "test"):
                state = 3
            elif ax.mode == "home":
                state = 4
            elif ax.mode == "stopping":
                state = 5
            else:
                state = 1 if (ax.servo_on or self.p(ax.n, 0x129) == 0) else 0
            st = 0
            if state == 1:
                st |= R.ST_READY
            if ax.running:
                st |= R.ST_BUSY
            if ax.done:
                st |= R.ST_DONE
            if ax.homed:
                st |= R.ST_HOMED
            if ax.alarm:
                st |= R.ST_ALARM
            if self.latch or self.stop_active:
                st |= R.ST_ESTOP
            if ax.lim["+"]:
                st |= 1 << 6
            if ax.lim["-"]:
                st |= 1 << 7
            if svon:
                st |= R.ST_SVON
            if ax.posinv:
                st |= R.ST_POSINV
            if ax.sw & 1:
                st |= R.ST_SWP
            if ax.sw & 2:
                st |= R.ST_SWN
            pos = int(round(ax.pos))
            um, ppr, lead, gn, gd = self._unit_params(ax.n)
            self._set(b + R.A_STATE, state)
            self._set(b + R.A_STATUS, st)
            self._setw(b + R.A_CMDPOS, 2, pos)
            self._setw(b + R.A_CMDSPEED, 2, int(ax.vel))
            self._setw(b + R.A_TARGET_ACT, 2, int(ax.target))
            self._set(b + R.A_ALARM, ax.alarm)
            self._set(b + R.A_ALARM2, ax.alarm)
            self._set(b + R.A_ALARM_COUNT, ax.alarm_count)
            self._set(b + R.A_STOPCAUSE, ax.stop_cause)
            self._set(b + R.A_INPUTS, (1 << 1 if ax.lim["+"] else 0) | (1 << 2 if ax.lim["-"] else 0))
            self._set(b + R.A_CMDRES, ax.cmd_result)
            self._setw(b + R.A_MOVECOUNT, 2, ax.move_count)
            done_now = int(abs(ax.pos - ax.start_pos)) if ax.running else ax.pulses_done
            self._setw(b + R.A_PULSES_DONE, 2, done_now)
            pu = max(-(1 << 31), min((1 << 31) - 1, units.to_units(pos, um, ppr, lead, gn, gd) if um else pos * 1000))
            self._setw(b + R.A_POSUNIT, 2, pu)
            self._set(b + R.A_MAXRPM, units.max_rpm(self.p(ax.n, 0x104), ppr))
            any_alarm |= bool(ax.alarm)
            mask |= (1 << (ax.n - 1)) if ax.alarm else 0
        hint = 0x0101 if (self.stop_active or self.latch) else next((a.alarm for a in self.axes if a.alarm), self.sys_alarm)
        self._set(R.ALARM_HINT, hint)
        self._set(R.ALARM_AXIS_MASK, mask)
        self._set(R.SYS_STATUS, (self.stop_active) | (self.latch << 1) | ((any_alarm or bool(self.sys_alarm)) << 2) |
                  (self.unsaved << 3))
        self._set(R.ESTOP_STATE, 1 if self.stop_active else (2 if self.latch else 0))
        self._set(R.SYS_INPUTS, int(self.stop_active))
        self._set(R.TCP_CLIENTS, self.clients)
        self._setw(R.UPTIME_S, 2, int(self.uptime))
        self._set(R.HEARTBEAT, int(self.uptime * 10))
        self._set(R.RESET_REASON, self.reset_reason)
        self._set(R.COMM_MODE_ACTIVE, 0)
        self._set(R.SYS_CMD_RESULT, self.sys_result)
        di = 0
        if self.stop_active:
            di |= 1 << 1 | (1 << 5 if self.axes_n == 2 else 0)
        self._set(R.DI_RAW, di)
        do = sum((1 << (a.n - 1)) for a in self.axes if a.servo_on or a.running)
        do |= (self.regs[R.USER_DO] & 3) << 2
        self._set(R.DO_STATE, do)
        self._set(R.WIRING_STATUS, self._wiring_word())
        self._set(R.HIST_COUNT, len(self.history))
        self._set(R.HIST_COUNT + 1, 0)
        for i, (code, axis, up) in enumerate(self.history):
            b = R.HIST_BASE + 4 * i
            self._set(b, code)
            self._set(b + 1, axis)
            self._setw(b + 2, 2, up)
        mac = (0x0200, 0x5356, 0x0001)
        for i, v in enumerate(mac):
            self._set(0x4007 + i, v)
        # legacy window (axis 1)
        a1 = self.axes[0]
        self._setw(0x0006, 2, self.regs[R.AXIS_BASE[1] + R.A_PULSES_DONE] | (self.regs[R.AXIS_BASE[1] + R.A_PULSES_DONE + 1] << 16))
        lst = (int(self.eth_link)) | (int(self.clients > 0) << 1) | (int(a1.servo_on) << 2) | (int(a1.running) << 3) | \
              (int(a1.legacy_result == 1) << 4) | (int(a1.legacy_fault != 0) << 5)
        self._set(0x000A, lst)
        self._set(0x000D, a1.legacy_result)
        self._set(0x000E, a1.legacy_fault)
        self._set(0x000F, self.clients)
        self._set(0x0010, int(self.eth_link))
        self._setw(0x0012, 2, min(5000, self.p(1, 0x0) if False else self._getn(0x0002)))

    def _wiring_word(self):
        w = 0
        for ax in self.axes:
            b = (ax.n - 1) * 4
            bits = {R.W_STOP_OK: not self.stop_active, R.W_START_WAIT: True, R.W_LOOPBACK: True,
                    R.W_SVON_OUT: ax.servo_on or ax.running}
            for k, v in bits.items():
                v = self.wiring_override.get(b + k, v)
                w |= int(bool(v)) << (b + k)
        for bit, dv in ((R.W_ETH_LINK, self.eth_link), (R.W_RTU_RX, False), (R.W_VALID, True)):
            w |= int(bool(self.wiring_override.get(bit, dv))) << bit
        return w

    # ---- fault injection (console / tests) -----------------------------------
    def set_stop_input(self, active: bool):
        with self.lock:
            self.update()
            if active and not self.stop_active:
                self.stop_active = True
                self.latch = True
                for ax in self.axes:
                    if ax.running:
                        self._finish(ax, 0x0101, False)
                    ax.alarm = 0x0101
                    ax.alarm_count += 1
                    if self.p(ax.n, 0x127) == 0:
                        ax.posinv = True
                    self._hist(0x0101, ax.n)
            elif not active:
                self.stop_active = False
            self._refresh()

    def _release_latch(self):
        self.latch = False
        for ax in self.axes:
            if ax.alarm == 0x0101:
                ax.alarm = 0

    def inject_alarm(self, axis, code):
        with self.lock:
            self.update()
            if axis == 0:
                self.sys_alarm = code
            else:
                ax = self.axes[axis - 1]
                if ax.running:
                    self._finish(ax, code, False)
                ax.alarm = code
                ax.alarm_count += 1
            self._hist(code, axis)
            self._refresh()

    def clear_alarms(self):
        with self.lock:
            self.sys_alarm = 0
            for ax in self.axes:
                if ax.alarm != 0x0101:
                    ax.alarm = 0
            self._refresh()

    def _hist(self, code, axis):
        self.history.insert(0, (code, axis, int(self.uptime)))
        del self.history[R.HIST_N:]

    def set_wiring(self, bit, value):
        with self.lock:
            if value is None:
                self.wiring_override.pop(bit, None)
            else:
                self.wiring_override[bit] = value
            self._refresh()

    # ---- Modbus-facing API ------------------------------------------------------
    def read_regs(self, addr, qty):
        with self.lock:
            self.update()
            out = []
            for a in range(addr, addr + qty):
                if a in self.fields:
                    f = self.fields[a][0]
                    out.append(0 if f.name in ("axis_command", "sys_command", "unlock_key", "net_apply") or
                               f.name.endswith(".axis_command") else self.regs[a])
                elif a in self.reserved_legacy:
                    out.append(0)
                else:
                    raise SimException(2)
            return out

    def write_regs(self, addr, vals):
        with self.lock:
            self.update()
            n = len(vals)
            for i in range(n):
                a = addr + i
                if a not in self.fields or not self.fields[a][0].rw:
                    raise SimException(2)
            items = []
            i = 0
            while i < n:
                f, idx = self.fields[addr + i]
                if idx != 0 or i + f.words > n:
                    raise SimException(3)
                items.append((f, [v & 0xFFFF for v in vals[i:i + f.words]]))
                i += f.words
            locked = not self.unlocked()
            for f, w in items:
                v = R.decode(f.kind, w)
                if f.lo is not None and f.hi is not None and not (f.lo <= v <= f.hi):
                    raise SimException(3)
                cur = [self.regs[f.addr + k] for k in range(f.words)]
                if f.net and locked and w != cur:
                    raise SimException(3)
                if f.name == "unlock_key" and v != R.UNLOCK_KEY:
                    self.unlock_until = -1.0
                    raise SimException(3)
            for f, w in items:
                self._apply(f, w)
            self._refresh()

    def _apply(self, f, w):
        name = f.name
        if name == "unlock_key":
            self.unlock_until = self.clock() + R.UNLOCK_WINDOW_S
            return
        if name == "sys_command":
            self.sys_result = self._sys_cmd(w[0])
            return
        if name == "net_apply":
            if w[0] == 1:
                self.nvm = self._snapshot()
                self.unsaved = False
            return
        if name.endswith("axis_command"):
            n = 1 if f.addr < 0x2000 else 2
            self._axis_cmd(self.axes[n - 1], w[0])
            return
        changed = any(self.regs[f.addr + k] != w[k] for k in range(f.words))
        for k in range(f.words):
            self.regs[f.addr + k] = w[k]
        if f.persist and changed:
            self.unsaved = True

    # ---- system commands -------------------------------------------------------
    def _sys_cmd(self, cmd):
        needs_lock = cmd in (R.CMD_LOAD_DEFAULT, R.CMD_REBOOT, R.CMD_FACTORY_RESET, R.CMD_NET_RESET)
        if needs_lock and not self.unlocked():
            return R.RESULT_LOCKED
        if cmd in (R.CMD_LOAD_DEFAULT, R.CMD_FACTORY_RESET, R.CMD_NET_RESET) and any(a.running for a in self.axes):
            return R.RESULT_RUNNING
        if cmd == R.CMD_ESTOP_RELEASE:      # D-072: no release command, start command releases the latch
            return R.RESULT_REFUSED
        if cmd == R.CMD_SAVE:
            self.nvm = self._snapshot()
            self.unsaved = False
        elif cmd in (R.CMD_LOAD_DEFAULT, R.CMD_FACTORY_RESET):
            self._defaults_all()
            if cmd == R.CMD_FACTORY_RESET:
                self.nvm = self._snapshot()
                self.unsaved = False
        elif cmd == R.CMD_NET_RESET:
            self._defaults_all(only_net=True)
            self.nvm = self._snapshot()
            self.unsaved = False
        elif cmd == R.CMD_REBOOT:
            for a, v in self.nvm.items():
                self.regs[a] = v
            for ax in self.axes:
                ax.__init__(ax.n)
            self.latch = False
            self.history = []
            self.uptime = 0.0
            self.unlock_until = -1.0
            self.reset_reason = 2
            self.unsaved = False
        elif cmd == R.CMD_CLEAR_HIST:
            self.history = []
        elif cmd == R.CMD_STOP_ALL:
            for ax in self.axes:
                self._begin_stop(ax, 2)
        else:
            return R.RESULT_REFUSED
        return R.RESULT_OK

    # ---- axis commands -------------------------------------------------------------
    def _limits_ok(self, ax, target):
        if self.p(ax.n, 0x11A) & 1:
            if target > self.p(ax.n, 0x11B) or target < self.p(ax.n, 0x11D):
                return False
        return True

    def _start_move(self, ax, target, speed, abs_cmd):
        """Returns cmd_result code."""
        n = ax.n
        maxs = self.p(n, 0x104)
        if speed < 1 or speed > min(units.MAX_PPS, maxs):
            return R.CR_RANGE
        if abs_cmd and ax.posinv and self.p(n, 0x128):
            return R.CR_POSINV
        if not -(1 << 31) <= target < (1 << 31):
            return R.CR_RANGE
        if not self._limits_ok(ax, target):
            return R.CR_SWLIM
        d = target - ax.pos
        if (d > 0 and ax.lim["+"]) or (d < 0 and ax.lim["-"]):
            return R.CR_LIMDIR
        ax.sw = 0
        ax.done = False
        ax.legacy_result = 0
        ax.start_pos = ax.pos
        ax.pulses_done = 0
        ax.target = int(target)
        ax.speed = speed
        if d == 0:
            self._finish(ax, 1, True)
        else:
            ax.mode = "move"
        return R.CR_OK

    def _axis_cmd(self, ax, cmd):
        n = ax.n
        ax.cmd_result = self._axis_cmd_inner(ax, cmd)
        if cmd in (1, 2, 13, 14, 17) and ax.cmd_result and n == 1:
            ax.legacy_fault = 1

    def _axis_cmd_inner(self, ax, cmd):
        n = ax.n
        starters = (1, 2, 4, 8, 9, 13, 14, 15, 16, 17)
        if cmd in starters:
            if self.stop_active:
                return R.CR_ESTOP
            if self.latch:
                self._release_latch()          # D-072
            if ax.alarm:
                return R.CR_ALARM
            hold = cmd in (15, 16) and ax.mode == "test" and ax.dir == (1 if cmd == 15 else -1)
            if ax.running and not hold:
                return R.CR_RUNNING
            if self.p(n, 0x129) == 1 and not ax.servo_on:
                return R.CR_SVOFF
        if cmd == 1 or cmd == 2:
            t = self._get(self.fields[R.AXIS_BASE[n] + R.A_TARGET][0])
            sp = self._get(self.fields[R.AXIS_BASE[n] + R.A_SPEED][0])
            return self._start_move(ax, (ax.pos + t) if cmd == 2 else t, sp, cmd == 1)
        if cmd in (13, 14):
            um, ppr, lead, gn, gd = self._unit_params(n)
            tu = self._get(self.fields[R.AXIS_BASE[n] + R.A_TARGET_UNIT][0])
            su = self._get(self.fields[R.AXIS_BASE[n] + R.A_SPEED_UNIT][0])
            tp = units.to_pulses(tu, um, ppr, lead, gn, gd)
            sp = abs(units.speed_to_pps(su, um, ppr, lead, gn, gd))
            return self._start_move(ax, (int(round(ax.pos)) + tp) if cmd == 14 else tp, sp, cmd == 13)
        if cmd == 17:
            idx = self.p(n, R.A_POINT_INDEX)
            base = R.POINT_BASE + (n - 1) * 0x80 + idx * 0x10
            if idx >= R.POINT_COUNT or not self._get(self.fields[base + 6][0]):
                return R.CR_RANGE
            um, ppr, lead, gn, gd = self._unit_params(n)
            tp = units.to_pulses(self._get(self.fields[base][0]), um, ppr, lead, gn, gd)
            sp = abs(units.speed_to_pps(self._get(self.fields[base + 2][0]), um, ppr, lead, gn, gd))
            return self._start_move(ax, tp, sp, True)
        if cmd == 18:
            if ax.running:
                return R.CR_RUNNING
            idx = self.p(n, R.A_POINT_INDEX)
            base = R.POINT_BASE + (n - 1) * 0x80 + idx * 0x10
            um, ppr, lead, gn, gd = self._unit_params(n)
            pu = units.to_units(int(round(ax.pos)), um, ppr, lead, gn, gd) if um else int(round(ax.pos)) * 1000
            self._store(base, "s32", pu)
            self._store(base + 2, "u32", self.p(n, R.A_SPEED_UNIT))
            self._store(base + 4, "u16", self.p(n, R.A_ACCEL))
            self._store(base + 5, "u16", self.p(n, R.A_DECEL))
            self._store(base + 6, "u16", 1)
            self.unsaved = True
            return R.CR_OK
        if cmd in (15, 16):
            um, ppr, lead, gn, gd = self._unit_params(n)
            sp = units.rpm_to_pps(self.p(n, 0x147), ppr)
            if sp < 1 or sp > min(units.MAX_PPS, self.p(n, 0x104)):
                return R.CR_RANGE
            d = 1 if cmd == 15 else -1
            if (d > 0 and ax.lim["+"]) or (d < 0 and ax.lim["-"]):
                return R.CR_LIMDIR
            if not (ax.mode == "test" and ax.dir == d):
                ax.start_pos = ax.pos
            ax.mode, ax.dir, ax.speed = "test", d, sp
            ax.t_left = self.clock() + TEST_HOLD_S
            ax.done = False
            ax.sw = 0
            return R.CR_OK
        if cmd in (8, 9):
            ov = self.p(n, R.A_JOGOVR)
            jm = self.p(n, 0x10E)
            sp = ov if ov else self.p(n, 0x10C)
            if sp < 1 or sp > jm:
                return R.CR_RANGE
            d = 1 if cmd == 8 else -1
            if (d > 0 and ax.lim["+"]) or (d < 0 and ax.lim["-"]):
                return R.CR_LIMDIR
            ax.start_pos = ax.pos
            ax.mode, ax.dir, ax.speed = "jog", d, sp
            ax.done = False
            ax.sw = 0
            return R.CR_OK
        if cmd == 4:
            ax.mode, ax.t_left, ax.start_pos, ax.done, ax.speed = "home", HOME_SECONDS, ax.pos, False, self.p(n, 0x113)
            return R.CR_OK
        if cmd == 3 or cmd == 10:
            self._begin_stop(ax, 2 if cmd == 3 else 4)
            return R.CR_OK
        if cmd == 12:
            if ax.running:
                self._finish(ax, 3, False)
            return R.CR_OK
        if cmd == 5:
            if ax.alarm != 0x0101:
                ax.alarm = 0
            return R.CR_OK
        if cmd in (6, 7):
            if ax.running:
                return R.CR_RUNNING
            ax.servo_on = cmd == 6
            return R.CR_OK
        if cmd == 11:
            if ax.running:
                return R.CR_RUNNING
            ax.pos = float(self._get(self.fields[R.AXIS_BASE[n] + R.A_TARGET][0]))
            return R.CR_OK
        return R.CR_RANGE

    def _store(self, addr, kind, v):
        for i, w in enumerate(R.encode(kind, v)):
            self.regs[addr + i] = w

    # ---- coils / discrete inputs ------------------------------------------------------
    def read_coils(self, addr, qty):
        with self.lock:
            self.update()
            out = []
            for a in range(addr, addr + qty):
                out.append(self._coil(a))
            return out

    def _coil(self, a):
        a1 = self.axes[0]
        if a == 0 or a == 2:
            return False
        if a == 1:
            return a1.servo_on
        if a == 3:
            return self.coil_dir
        if 4 <= a <= 7:
            return False
        for ax in self.axes:
            b = R.LEGACY_COIL_AXIS[ax.n]
            if b <= a < b + 4:
                o = a - b
                return [ax.mode == "jog" and ax.dir > 0, ax.mode == "jog" and ax.dir < 0, False, ax.servo_on][o]
        raise SimException(2)

    def write_coil(self, a, on):
        with self.lock:
            self.update()
            self._coil(a)
            a1 = self.axes[0]
            if a == 0:
                if on:
                    pulses = self._getn(0x0000)
                    sp = self._getn(0x0002)
                    if pulses > 0x7FFFFFFF:
                        a1.cmd_result = R.CR_RANGE
                        a1.legacy_fault = 1
                    else:
                        a1.legacy_fault = 0
                        pre = self.stop_active
                        res = self._axis_cmd_start_rel(a1, pulses if self.coil_dir else -pulses, sp)
                        a1.cmd_result = res
                        if res:
                            a1.legacy_fault = 1
            elif a == 1:
                a1.servo_on = on and not a1.running
            elif a == 2:
                if on:
                    if a1.running:
                        self._finish(a1, 3, False)
                    a1.pulses_done, a1.legacy_result, a1.legacy_fault = 0, 0, 0
            elif a == 3:
                self.coil_dir = on
            elif a >= 8:
                for ax in self.axes:
                    b = R.LEGACY_COIL_AXIS[ax.n]
                    if b <= a < b + 4:
                        o = a - b
                        if o == 0:
                            ax.cmd_result = self._axis_cmd_inner(ax, 8) if on else self._axis_cmd_inner(ax, 10)
                        elif o == 1:
                            ax.cmd_result = self._axis_cmd_inner(ax, 9) if on else self._axis_cmd_inner(ax, 10)
                        elif o == 2 and on:
                            ax.cmd_result = self._axis_cmd_inner(ax, 3)
                        elif o == 3:
                            ax.cmd_result = self._axis_cmd_inner(ax, 6 if on else 7)
            self._refresh()

    def _axis_cmd_start_rel(self, ax, rel, speed):
        if self.stop_active:
            return R.CR_ESTOP
        if self.latch:
            self._release_latch()
        if ax.alarm:
            return R.CR_ALARM
        if ax.running:
            return R.CR_RUNNING
        return self._start_move(ax, int(round(ax.pos)) + rel, speed, False)

    def read_discrete(self, addr, qty):
        with self.lock:
            self.update()
            a1 = self.axes[0]
            vals = [a1.running, self.eth_link, self.clients > 0, a1.legacy_result == 1, a1.legacy_fault != 0]
            out = []
            for a in range(addr, addr + qty):
                if a < len(vals):
                    out.append(bool(vals[a]))
                elif a < 8:
                    out.append(False)
                else:
                    raise SimException(2)
            return out

    # ---- console -------------------------------------------------------------------------
    HELP = ("명령: stop on|off (STOP 입력 누름/해제) | alarm <축 0=시스템,1,2> <코드 예 0x0301> | clear (알람 소거) | "
            "limit <축> +|- on|off | wiring <x.stop|x.start|x.loop|y.stop|y.start|y.loop|eth|rtu> on|off|auto | "
            "link on|off | status | quit")

    def console(self, line):
        t = line.split()
        if not t:
            return ""
        c = t[0].lower()
        try:
            if c == "help":
                return self.HELP
            if c == "stop":
                self.set_stop_input(t[1] == "on")
            elif c == "alarm":
                self.inject_alarm(int(t[1]), int(t[2], 0))
            elif c == "clear":
                self.clear_alarms()
            elif c == "limit":
                with self.lock:
                    self.axes[int(t[1]) - 1].lim[t[2]] = t[3] == "on"
            elif c == "link":
                self.eth_link = t[1] == "on"
            elif c == "wiring":
                nm = t[1].lower()
                bit = {"eth": R.W_ETH_LINK, "rtu": R.W_RTU_RX}.get(nm)
                if bit is None:
                    base = 0 if nm[0] == "x" else 4
                    bit = base + {"stop": R.W_STOP_OK, "start": R.W_START_WAIT, "loop": R.W_LOOPBACK}[nm[2:]]
                self.set_wiring(bit, None if t[2] == "auto" else t[2] == "on")
            elif c == "status":
                self.update()
                return " | ".join(f"{'XY'[a.n-1]}: {a.mode} pos={a.pos:.0f} alarm=0x{a.alarm:04X}" for a in self.axes) + \
                    f" | stop_input={self.stop_active} latch={self.latch}"
            else:
                return "알 수 없는 명령. help 참고"
        except (IndexError, ValueError, KeyError):
            return "인수 오류. help 참고"
        return "ok"


# ---- TCP server --------------------------------------------------------------------------
class _Handler(socketserver.BaseRequestHandler):
    def handle(self):
        sim: Sim = self.server.sim
        with sim.lock:
            sim.clients += 1
        try:
            while True:
                hdr = self._recv(7)
                if not hdr:
                    return
                tid, proto, length, unit = struct.unpack(">HHHB", hdr)
                pdu = self._recv(length - 1)
                if pdu is None:
                    return
                resp = self.dispatch(sim, pdu)
                self.request.sendall(struct.pack(">HHHB", tid, 0, len(resp) + 1, unit) + resp)
        except OSError:
            pass
        finally:
            with sim.lock:
                sim.clients -= 1

    def _recv(self, n):
        buf = b""
        while len(buf) < n:
            c = self.request.recv(n - len(buf))
            if not c:
                return None
            buf += c
        return buf

    @staticmethod
    def dispatch(sim, pdu):
        fc = pdu[0]
        try:
            if fc in (3, 4):
                a, q = struct.unpack(">HH", pdu[1:5])
                if not 1 <= q <= 125:
                    raise SimException(3)
                v = sim.read_regs(a, q)
                return bytes([fc, q * 2]) + struct.pack(f">{q}H", *v)
            if fc in (1, 2):
                a, q = struct.unpack(">HH", pdu[1:5])
                if not 1 <= q <= 2000:
                    raise SimException(3)
                bits = sim.read_coils(a, q) if fc == 1 else sim.read_discrete(a, q)
                by = bytearray((q + 7) // 8)
                for i, b in enumerate(bits):
                    if b:
                        by[i // 8] |= 1 << (i % 8)
                return bytes([fc, len(by)]) + bytes(by)
            if fc == 5:
                a, v = struct.unpack(">HH", pdu[1:5])
                if v not in (0, 0xFF00):
                    raise SimException(3)
                sim.write_coil(a, v == 0xFF00)
                return pdu[:5]
            if fc == 6:
                a, v = struct.unpack(">HH", pdu[1:5])
                sim.write_regs(a, [v])
                return pdu[:5]
            if fc == 15:
                a, q, bc = struct.unpack(">HHB", pdu[1:6])
                if not 1 <= q <= 1968 or bc != (q + 7) // 8:
                    raise SimException(3)
                for i in range(q):
                    sim.write_coil(a + i, bool(pdu[6 + i // 8] >> (i % 8) & 1))
                return pdu[:5]
            if fc == 16:
                a, q, bc = struct.unpack(">HHB", pdu[1:6])
                if not 1 <= q <= 123 or bc != q * 2:
                    raise SimException(3)
                sim.write_regs(a, list(struct.unpack(f">{q}H", pdu[6:6 + q * 2])))
                return pdu[:5]
            raise SimException(1)
        except SimException as e:
            return bytes([fc | 0x80, e.code])
        except struct.error:
            return bytes([fc | 0x80, 3])


class SimServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, sim, host="127.0.0.1", port=5020):
        super().__init__((host, port), _Handler)
        self.sim = sim

    def start(self):
        t = threading.Thread(target=self.serve_forever, daemon=True)
        t.start()
        return t


def run_server(port=5020, axes=2, host="0.0.0.0", console=True):
    sim = Sim(axes)
    srv = SimServer(sim, host, port)
    print(f"시뮬레이터 시작: {host}:{srv.server_address[1]} ({axes}축, regmap {R.REGMAP_VERSION}). 종료는 Ctrl+C. 'help' 입력 시 시험 명령 안내")
    srv.start()
    try:
        if console and sys.stdin and sys.stdin.isatty():
            for line in sys.stdin:
                if line.strip() == "quit":
                    break
                out = sim.console(line)
                if out:
                    print(out)
        else:
            while True:
                time.sleep(1)
    except KeyboardInterrupt:
        pass
    finally:
        srv.shutdown()
    return 0
