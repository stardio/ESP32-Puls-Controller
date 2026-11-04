"""Local web UI for the controller (python -m svtool web --target IP).

Standard library only. The browser talks to this process over HTTP; this process talks to the controller over
Modbus (one shared client, serialised by a lock). Safety rules [REC, rule 9]:
- binds to 127.0.0.1 by default, so other PCs on the plant LAN cannot drive the axes;
- every POST must carry the X-Svtool header and a JSON body (a plain cross-site form cannot send either) and the
  Host header must be local (blocks DNS rebinding);
- hold-to-run uses TEST_POS/NEG, which the firmware stops by itself 500 ms after the last keepalive (FR-032), so a
  closed tab, a crashed browser or a lost link all end in a decel stop.
"""
import json
import os
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

from . import alarms, backup as B, interp as IPM, regmap as R
from .device import Device, open_target
from .modbus import ModbusError, ModbusException

PAGE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "web_ui.html")
LOCAL_HOSTS = ("127.0.0.1", "localhost", "[::1]")

AXIS_STATE = {0: "서보 OFF", 1: "대기", 2: "위치결정 운전", 3: "JOG", 4: "원점복귀", 5: "감속 정지 중", 6: "알람 정지",
              7: "비상정지", 8: "단독 운전"}
STATUS_BITS = [(0, "ready"), (1, "busy"), (2, "완료"), (3, "원점 완료"), (4, "알람"), (5, "비상정지"), (6, "+리밋"),
               (7, "−리밋"), (8, "드라이브 알람"), (9, "INP"), (10, "서보 ON"), (11, "위치 무효"), (12, "+소프트 리밋"),
               (13, "−소프트 리밋"), (14, "통신두절 정지"), (15, "언더런")]
CMD_RESULT = {0: "수락", 1: "실행 중", 0x10: "거부: 비상정지", 0x11: "거부: 알람", 0x12: "거부: 서보 OFF", 0x13: "거부: 운전 중",
              0x14: "거부: 소프트 리밋 초과", 0x15: "거부: 리밋 입력 활성 방향", 0x16: "거부: 위치 무효(원점복귀 필요)",
              0x17: "거부: 값 범위"}
STOP_CAUSE = {0: "-", 1: "이동 완료", 2: "STOP 명령", 3: "즉시 정지", 4: "JOG 정지"}
SYS_STATUS_BITS = [(0, "STOP 입력"), (1, "비상정지 래치"), (2, "알람"), (3, "미저장 변경"), (4, "저장 중"), (5, "저장 오류"),
                   (6, "통신두절")]
# Commands the page may send. Motion-start commands write their operands first (see Bridge.axis_command).
PAGE_COMMANDS = {"START_ABS", "START_REL", "STOP", "QUICK_STOP", "ALARM_RESET", "SERVO_ON", "SERVO_OFF", "TEST_POS",
                 "TEST_NEG", "HOME", "SET_POSITION"}


# HELP tab pin table. Values must equal the product firmware Kconfig defaults (firmware/main/Kconfig.projbuild,
# design-baseline-v0.4 sec.2, D-069); tests/test_web.py checks that. Rows: (Kconfig name or "", GPIO, group, signal,
# direction, polarity / note, connector).
PIN_TABLE = [
    ("FW_X_PLS_GPIO", 22, "펄스 출력", "X축 펄스 (PLS)", "출력", "ISO7720 → AM26C31 1A, 차동 출력 (D-087: 25 → 22)", "J7 PULS(X)"),
    ("FW_X_DIR_GPIO", 32, "펄스 출력", "X축 방향 (DIR)", "출력", "ISO7720 → AM26C31 2A, 차동 출력", "J7 PULS(X)"),
    ("FW_Y_PLS_GPIO", 20, "펄스 출력", "Y축 펄스 (PLS)", "출력", "ISO7720 U16 → AM26C31 3A (1축 모델 미사용)", "J13 PULS(Y)"),
    ("FW_Y_DIR_GPIO", 21, "펄스 출력", "Y축 방향 (DIR)", "출력", "AM26C31 4A (1축 모델 미사용)", "J13 PULS(Y)"),
    ("FW_OE_GPIO", 33, "펄스 출력", "펄스 출력 허용 (두 축 공용)", "출력", "HIGH = 허용, 부팅 시 LOW (TLP2361 → AM26C31 /G)", "–"),
    ("FW_X_START_GPIO", 54, "입력", "X축 START (DI1)", "입력", "엣지 동작, LOW 활성", "J15 D/I(X)"),
    ("FW_X_STOP_GPIO", 19, "입력", "X축 STOP (DI2)", "입력", "NC 접점, HIGH 활성 = 정지. MCPWM 하드웨어 차단 경로", "J15 D/I(X)"),
    ("FW_X_DI3_GPIO", 18, "입력", "X·Y 동시 START (DI3)", "입력", "엣지 동작, LOW 활성. 두 축 상대 이동 동시 시작(한 축이라도 불가면 둘 다 안 함), D-088", "J15 D/I(X)"),
    ("FW_X_DI4_GPIO", 17, "입력", "X·Y 동시 STOP (DI4)", "입력", "엣지 동작, LOW 활성. 두 축 감속 정지(안전 비상정지 아님), D-088", "J15 D/I(X)"),
    ("FW_Y_START_GPIO", 7, "입력", "Y축 START (DI5)", "입력", "엣지 동작, LOW 활성", "J16 D/I(Y)"),
    ("FW_Y_STOP_GPIO", 8, "입력", "Y축 STOP (DI6)", "입력", "NC 접점, HIGH 활성 = 정지. 미배선이면 정지 상태로 읽힘", "J16 D/I(Y)"),
    ("FW_Y_DI7_GPIO", 15, "입력", "Y축 예비 DI7 (원점·리밋용)", "입력", "LOW 활성, 레지스터로만 읽음", "J16 D/I(Y)"),
    ("FW_Y_DI8_GPIO", 16, "입력", "Y축 예비 DI8 (원점·리밋용)", "입력", "LOW 활성, 레지스터로만 읽음", "J16 D/I(Y)"),
    ("FW_DO1_GPIO", 23, "출력", "DO1 X축 서보 ON", "출력", "드라이브 SVON", "–"),
    ("FW_DO2_GPIO", 26, "출력", "DO2 Y축 서보 ON", "출력", "드라이브 SVON", "–"),
    ("FW_DO3_GPIO", 27, "출력", "DO3 범용 출력", "출력", "user_do b0", "–"),
    ("FW_DO4_GPIO", 46, "출력", "DO4 범용 출력", "출력", "user_do b1", "–"),
    ("FW_RS485_TX_GPIO", 14, "통신", "RS-485 TX", "출력", "RTU 모델 (D-087: 24 → 14)", "–"),
    ("FW_RS485_RX_GPIO", 47, "통신", "RS-485 RX", "입력", "RTU 모델", "–"),
    ("FW_RS485_DE_GPIO", 48, "통신", "RS-485 DE (송신 허용)", "출력", "RTU 모델, UART RTS", "–"),
    ("FW_PHY_RESET_GPIO", 51, "통신", "이더넷 PHY(IP101) 리셋", "출력", "보드 내장", "RJ45"),
    ("", 37, "보드", "콘솔 UART TX", "출력", "보드 USB-C의 CH343 USB-UART 경유, 115200 bps (플래시·로그)", "USB-C"),
    ("", 38, "보드", "콘솔 UART RX", "입력", "보드 USB-C의 CH343 USB-UART 경유", "USB-C"),
    ("", 24, "USB", "내장 USB Serial/JTAG 신호", "입출력", "펌웨어가 쓰지 않음(D-087). D−/D+ 중 어느 쪽인지는 데이터시트 확인 필요", "헤더"),
    ("", 25, "USB", "내장 USB Serial/JTAG 신호", "입출력", "펌웨어가 쓰지 않음(D-087). USB 선을 연결하면 두 번째 COM 포트(로그·JTAG)", "헤더"),
]


def help_info():
    return {"pins": [{"kconfig": k, "gpio": g, "group": grp, "signal": sig, "dir": d, "note": note, "connector": con}
                     for k, g, grp, sig, d, note, con in PIN_TABLE],
            "source": "제품 펌웨어 Kconfig 기본값(firmware/main/Kconfig.projbuild), design-baseline-v0.4 §2, D-069"}


def bits(word, table):
    return [name for b, name in table if word >> b & 1]


class Bridge:
    """Owns the Modbus connection; reconnects after a communication error."""

    def __init__(self, target, unit=1, timeout=1.0, opener=open_target):
        self.target, self.unit, self.timeout, self.opener = target, unit, timeout, opener
        self.lock = threading.Lock()
        self.dev = None
        self.ident = None
        self.alarm_table = alarms.load()

    def _device(self):
        if self.dev is None:
            self.dev = Device(self.opener(self.target, unit=self.unit, timeout=self.timeout))
            self.ident = self.dev.identity()
        return self.dev

    def _drop(self):
        if self.dev is not None:
            try:
                self.dev.c.close()
            except OSError:
                pass
        self.dev = None

    def call(self, fn):
        """Run fn(dev) under the lock. Communication errors close the link so the next call reconnects."""
        with self.lock:
            try:
                return fn(self._device())
            except (ModbusError, OSError) as e:
                if not isinstance(e, ModbusException):
                    self._drop()
                raise

    # ---- reads -------------------------------------------------------------------------------------------
    def state(self):
        def rd(dev):
            sysw = dev.read_words(R.MODEL_ID, R.COMM_MODE_ACTIVE - R.MODEL_ID + 1)
            sys2 = dev.read_words(R.ALARM_AXIS_MASK, R.ALARM_HINT - R.ALARM_AXIS_MASK + 1)
            s = lambda a: sysw[a - R.MODEL_ID]
            s32 = lambda a: sysw[a - R.MODEL_ID] | sysw[a - R.MODEL_ID + 1] << 16
            out = {"connected": True, "target": self.target, "rtt_ms": round(getattr(dev.c, "last_rtt_us", 0) / 1000, 1),
                   "model_id": s(R.MODEL_ID), "axis_count": s(R.AXIS_COUNT),
                   "fw_version": R.fw_version_str(s32(R.FW_VERSION)), "regmap_version": s(R.REGMAP_VER),
                   "regmap_ok": s(R.REGMAP_VER) == R.REGMAP_VERSION,
                   "sys_status": bits(s(R.SYS_STATUS), SYS_STATUS_BITS), "unsaved": bool(s(R.SYS_STATUS) >> 3 & 1),
                   "estop_state": s(R.ESTOP_STATE), "tcp_clients": s(R.TCP_CLIENTS), "uptime_s": s32(R.UPTIME_S),
                   "heartbeat": s(R.HEARTBEAT), "boot_status": s(R.BOOT_STATUS), "comm_mode": s(R.COMM_MODE_ACTIVE),
                   "di_raw": sys2[R.DI_RAW - R.ALARM_AXIS_MASK], "do_state": sys2[R.DO_STATE - R.ALARM_AXIS_MASK]}
            wiring = sys2[R.WIRING_STATUS - R.ALARM_AXIS_MASK]
            items, ok = B.wiring_report(wiring, out["axis_count"], out["comm_mode"])
            out["wiring"] = {"word": wiring, "ok": ok, "items": [{"label": l, "ok": g, "hint": h} for l, g, h in items]}
            hint = sys2[R.ALARM_HINT - R.ALARM_AXIS_MASK]
            out["alarm_hint"] = self._alarm(hint)
            out["axes"] = [self._axis(dev, n) for n in range(1, min(out["axis_count"], 2) + 1)]
            ips, ipr = dev.read_words(R.IP_STATE, 2)
            out["ip"] = {"state": ips, "state_text": R.IP_STATES.get(ips, str(ips)), "result": ipr,
                         "result_text": R.IP_RESULTS.get(ipr, f"0x{ipr:02X}")}
            out["t_ms"] = round(time.monotonic() * 1000)
            return out
        return self.call(rd)

    def _alarm(self, code):
        name, cause, action = alarms.describe(code, self.alarm_table)
        return {"code": code, "name": name, "cause": cause, "action": action}

    def _axis(self, dev, n):
        b = R.AXIS_BASE[n]
        w = dev.read_words(b, 0x10)
        u = dev.read_words(b + R.A_POSUNIT, 3)
        op = dev.read_words(b + R.A_TARGET, R.A_DECEL - R.A_TARGET + 1)
        d = lambda off, kind: R.decode(kind, w[off:off + 2])
        st = w[R.A_STATUS]
        return {"n": n, "name": "XY"[n - 1], "state": w[R.A_STATE], "state_text": AXIS_STATE.get(w[R.A_STATE], f"{w[R.A_STATE]}"),
                "status": st, "status_bits": bits(st, STATUS_BITS), "servo_on": bool(st >> 10 & 1),
                "busy": bool(st >> 1 & 1), "position": d(R.A_CMDPOS, "s32"), "speed": d(R.A_CMDSPEED, "s32"),
                "target_act": d(R.A_TARGET_ACT, "s32"), "alarm": self._alarm(w[R.A_ALARM]),
                "stop_cause": STOP_CAUSE.get(w[R.A_STOPCAUSE], f"0x{w[R.A_STOPCAUSE]:04X}"),
                "inputs": w[R.A_INPUTS], "cmd_result": w[R.A_CMDRES],
                "cmd_result_text": CMD_RESULT.get(w[R.A_CMDRES], f"0x{w[R.A_CMDRES]:02X}"),
                "move_count": d(R.A_MOVECOUNT, "u32"), "pulses_done": d(R.A_PULSES_DONE, "u32"),
                "position_unit": R.decode("s32", u[0:2]), "max_rpm": u[2],
                "operands": {"target": R.decode("s32", op[0:2]), "speed": R.decode("u32", op[2:4]),
                             "accel_ms": op[4], "decel_ms": op[5]}}

    def params(self, n):
        fields = R.axis_param_fields(n)

        def rd(dev):
            out = []
            for run in R.runs(fields):
                words = dev.read_words(run[0].addr, sum(f.words for f in run))
                pos = 0
                for f in run:
                    out.append({"name": f.name.split(".", 1)[1], "addr": f.addr, "kind": f.kind, "lo": f.lo, "hi": f.hi,
                                "default": f.default, "value": R.decode(f.kind, words[pos:pos + f.words])})
                    pos += f.words
            return out
        return self.call(rd)

    # ---- writes ------------------------------------------------------------------------------------------
    def write_params(self, n, values):
        by_name = {f.name.split(".", 1)[1]: f for f in R.axis_param_fields(n)}
        errors, plan = [], []
        for name, v in values.items():
            f = by_name.get(name)
            if f is None:
                errors.append(f"{name}: 알 수 없는 항목")
                continue
            try:
                v = int(v)
            except (TypeError, ValueError):
                errors.append(f"{name}: 정수가 아닙니다")
                continue
            if (f.lo is not None and v < f.lo) or (f.hi is not None and v > f.hi):
                errors.append(f"{name}: 범위 {f.lo}~{f.hi} 밖")
                continue
            plan.append((f, v))
        if errors:
            return {"ok": False, "errors": errors}

        def wr(dev):
            errs = []
            for f, v in plan:
                try:
                    dev.write(f, v)
                except ModbusException as e:
                    errs.append(f"{f.name}: 장치 거부 ({e})")
            return errs
        errs = self.call(wr)
        return {"ok": not errs, "errors": errs, "written": len(plan) - len(errs)}

    def sys_command(self, cmd):
        res = self.call(lambda dev: dev.sys_command(cmd))
        return {"ok": res == R.RESULT_OK, "result": res, "text": B.RESULT_TEXT.get(res, f"0x{res:02X}")}

    def axis_command(self, n, name, operands=None):
        if name not in PAGE_COMMANDS or n not in (1, 2):
            return {"ok": False, "text": "허용되지 않은 명령"}
        b = R.AXIS_BASE[n]
        op = operands or {}
        rt = {f.name.split(".", 1)[1]: f for f in R.axis_runtime_fields(n)}
        writes = []
        if name in ("START_ABS", "START_REL", "SET_POSITION") and "target" in op:
            writes.append((rt["target_position"], int(op["target"])))
        if name in ("START_ABS", "START_REL"):
            for key, fname in (("speed", "move_speed"), ("accel_ms", "accel_time_ms"), ("decel_ms", "decel_time_ms")):
                if key in op:
                    f, v = rt[fname], int(op[key])
                    if (f.lo is not None and v < f.lo) or (f.hi is not None and v > f.hi):
                        return {"ok": False, "text": f"{key}: 범위 {f.lo}~{f.hi} 밖"}
                    writes.append((f, v))

        def go(dev):
            for f, v in writes:
                dev.write(f, v)
            dev.c.write_register(b + R.A_CMD, R.AC[name])
            time.sleep(0.03)                        # firmware applies commands from its 1 ms poll task
            return dev.read_words(b + R.A_CMDRES, 1)[0]
        res = self.call(go)
        return {"ok": res in (R.CR_OK, 1), "result": res, "text": CMD_RESULT.get(res, f"0x{res:02X}")}

    def sync_command(self, cmd, absolute=False, operands=None):
        """D-088: X/Y simultaneous START (writes each axis's operands, then START_ALL_REL/ABS) or STOP (STOP_ALL)."""
        if cmd == "STOP":
            return self.sys_command(R.CMD_STOP_ALL)
        if cmd != "START":
            return {"ok": False, "text": "허용되지 않은 명령"}
        writes = []
        for key, op in (operands or {}).items():
            n = int(key)
            if n not in (1, 2):
                return {"ok": False, "text": f"축 번호 {key} 오류"}
            rt = {f.name.split(".", 1)[1]: f for f in R.axis_runtime_fields(n)}
            for k, fname in (("target", "target_position"), ("speed", "move_speed"), ("accel_ms", "accel_time_ms"),
                             ("decel_ms", "decel_time_ms")):
                if k not in op:
                    continue
                f, v = rt[fname], int(op[k])
                if (f.lo is not None and v < f.lo) or (f.hi is not None and v > f.hi):
                    return {"ok": False, "text": f"{'XY'[n - 1]}축 {k}: 범위 {f.lo}~{f.hi} 밖"}
                writes.append((f, v))

        def go(dev):
            for f, v in writes:
                dev.write(f, v)
            res = dev.sys_command(R.CMD_START_ALL_ABS if absolute else R.CMD_START_ALL_REL)
            axes = [dev.read_words(R.AXIS_BASE[n] + R.A_CMDRES, 1)[0] for n in range(1, min(self.ident["axis_count"], 2) + 1)]
            return res, axes
        res, axes = self.call(go)
        text = B.RESULT_TEXT.get(res, f"0x{res:02X}")
        if res != R.RESULT_OK:
            text += " — " + ", ".join(f"{'XY'[i]}축 {CMD_RESULT.get(c, f'0x{c:02X}')}" for i, c in enumerate(axes))
        return {"ok": res == R.RESULT_OK, "result": res, "text": text}

    # ---- D-089 interpolation -------------------------------------------------------------------------------
    IP_KEYS = ("ip_mode", "ip_abs", "ip_end_x", "ip_end_y", "ip_center_i", "ip_center_j", "ip_feed", "ip_accel_ms",
               "ip_decel_ms")

    def _ip_values(self, params):
        """Validate the page's parameters against the register ranges; returns {field: value}."""
        by_name = {f.name: f for f in R.INTERP}
        out = {}
        for k in self.IP_KEYS:
            f = by_name[k]
            try:
                v = int(params.get(k[3:], f.default))
            except (TypeError, ValueError):
                raise ValueError(f"{k[3:]}: 정수가 아닙니다")
            if (f.lo is not None and v < f.lo) or (f.hi is not None and v > f.hi):
                raise ValueError(f"{k[3:]}: 범위 {f.lo}~{f.hi} 밖")
            if f.kind == "s32" and not -2 ** 31 <= v < 2 ** 31:
                raise ValueError(f"{k[3:]}: 범위 밖")
            out[f] = v
        return out

    def interp_preview(self, params):
        """Plan the path on the PC with the firmware's maths (svtool/interp.py). Start = current positions."""
        try:
            vals = {f.name: v for f, v in self._ip_values(params).items()}
        except ValueError as e:
            return {"ok": False, "text": str(e)}
        start = self.call(lambda dev: [R.decode("s32", dev.read_words(R.AXIS_BASE[n] + R.A_CMDPOS, 2)) for n in (1, 2)])
        ex, ey = vals["ip_end_x"], vals["ip_end_y"]
        dx, dy = (ex - start[0], ey - start[1]) if vals["ip_abs"] else (ex, ey)
        try:
            p = IPM.Path(vals["ip_mode"], dx, dy, vals["ip_center_i"], vals["ip_center_j"], vals["ip_feed"],
                         vals["ip_accel_ms"], vals["ip_decel_ms"])
        except IPM.PlanError as e:
            return {"ok": False, "text": str(e)}
        vx, vy = p.axis_peak()
        pts = [[round(t, 4), round(start[0] + x, 2), round(start[1] + y, 2)] for t, x, y, _ in p.samples(400)]
        return {"ok": True, "start": start, "end": [start[0] + dx, start[1] + dy], "length": round(p.len, 1),
                "time": round(p.T, 3), "peak_feed": round(p.vpk, 1), "axis_peak": [round(vx), round(vy)],
                "radius": round(p.r, 1), "sweep_deg": round(abs(p.sweep) * 180 / 3.141592653589793, 2), "points": pts}

    def interp_start(self, params):
        try:
            vals = self._ip_values(params)
        except ValueError as e:
            return {"ok": False, "text": str(e)}

        def go(dev):
            for f, v in sorted(vals.items(), key=lambda kv: kv[0].addr):
                dev.write(f, v)
            t_ms = round(time.monotonic() * 1000)          # same clock as state()["t_ms"]: aligns the live trace
            res = dev.sys_command(R.CMD_INTERP_START)
            ipr = dev.read_words(R.IP_RESULT, 1)[0]
            return res, ipr, t_ms
        res, ipr, t_ms = self.call(go)
        text = "보간 시작" if res == R.RESULT_OK else f"{B.RESULT_TEXT.get(res, f'0x{res:02X}')} — {R.IP_RESULTS.get(ipr, f'0x{ipr:02X}')}"
        return {"ok": res == R.RESULT_OK, "result": res, "ip_result": ipr, "text": text, "t_ms": t_ms}

    def backup(self):
        return self.call(lambda dev: B.make_backup(dev, self.target))


class Handler(BaseHTTPRequestHandler):
    server_version = "svtool-web"
    bridge: Bridge = None

    def log_message(self, fmt, *args):          # keep the console quiet (status is polled 4x/s)
        pass

    def _send(self, code, body, ctype="application/json; charset=utf-8", extra=None):
        data = body if isinstance(body, bytes) else json.dumps(body, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(data)

    def _host_ok(self):
        host = (self.headers.get("Host") or "").rsplit(":", 1)[0]
        return host in LOCAL_HOSTS or getattr(self.server, "allow_any_host", False)

    def _guard(self, fn):
        if not self._host_ok():
            return self._send(403, {"ok": False, "text": "허용되지 않은 Host"})
        try:
            code, body = fn()
            return self._send(code, body) if not isinstance(body, tuple) else self._send(code, *body)
        except ModbusException as e:
            return self._send(200, {"ok": False, "connected": True, "text": f"장치 거부: {e}"})
        except (ModbusError, OSError) as e:
            return self._send(503, {"ok": False, "connected": False, "text": f"통신 오류: {e}"})

    def do_GET(self):
        u = urlparse(self.path)
        if u.path in ("/", "/index.html"):
            if not self._host_ok():
                return self._send(403, {"ok": False, "text": "허용되지 않은 Host"})
            with open(PAGE, "rb") as f:
                return self._send(200, f.read(), "text/html; charset=utf-8")
        q = parse_qs(u.query)
        if u.path == "/api/help":
            return self._guard(lambda: (200, help_info()))
        if u.path == "/api/state":
            return self._guard(lambda: (200, self.bridge.state()))
        if u.path == "/api/params":
            return self._guard(lambda: (200, {"ok": True, "params": self.bridge.params(int(q.get("axis", ["1"])[0]))}))
        if u.path == "/api/backup":
            def bk():
                data = json.dumps(self.bridge.backup(), ensure_ascii=False, indent=1).encode("utf-8")
                name = time.strftime("svtool-backup-%Y%m%d-%H%M%S.json")
                return 200, (data, "application/json; charset=utf-8", {"Content-Disposition": f'attachment; filename="{name}"'})
            return self._guard(bk)
        return self._send(404, {"ok": False, "text": "없는 경로"})

    def do_POST(self):
        if self.headers.get("X-Svtool") != "1" or not (self.headers.get("Content-Type") or "").startswith("application/json"):
            return self._send(403, {"ok": False, "text": "요청 형식 거부"})
        try:
            n = int(self.headers.get("Content-Length") or 0)
            body = json.loads(self.rfile.read(min(n, 65536)) or b"{}")
        except ValueError:
            return self._send(400, {"ok": False, "text": "JSON 오류"})
        path = urlparse(self.path).path
        if path == "/api/axis":
            return self._guard(lambda: (200, self.bridge.axis_command(int(body.get("axis", 0)), str(body.get("cmd")),
                                                                      body.get("operands"))))
        if path == "/api/params":
            return self._guard(lambda: (200, self.bridge.write_params(int(body.get("axis", 0)), body.get("values") or {})))
        if path == "/api/save":
            return self._guard(lambda: (200, self.bridge.sys_command(R.CMD_SAVE)))
        if path == "/api/interp/preview":
            return self._guard(lambda: (200, self.bridge.interp_preview(body)))
        if path == "/api/interp/start":
            return self._guard(lambda: (200, self.bridge.interp_start(body)))
        if path == "/api/sync":
            return self._guard(lambda: (200, self.bridge.sync_command(str(body.get("cmd")), body.get("mode") == "ABS",
                                                                      body.get("operands"))))
        if path == "/api/stop_all":
            return self._guard(lambda: (200, self.bridge.sys_command(R.CMD_STOP_ALL)))
        return self._send(404, {"ok": False, "text": "없는 경로"})


def make_server(bridge, host="127.0.0.1", port=8080):
    handler = type("BoundHandler", (Handler,), {"bridge": bridge})
    srv = ThreadingHTTPServer((host, port), handler)
    srv.daemon_threads = True
    srv.allow_any_host = host not in ("127.0.0.1", "localhost", "::1")
    return srv


def run(target, port=8080, host="127.0.0.1", unit=1, timeout=1.0, open_browser=True):
    bridge = Bridge(target, unit, timeout)
    srv = make_server(bridge, host, port)
    url = f"http://127.0.0.1:{srv.server_address[1]}/"
    print(f"웹 UI: {url}  (장치 {target}, 유닛 {unit}). 종료는 Ctrl+C")
    if srv.allow_any_host:
        print(f"주의: {host}에 열었습니다. 같은 네트워크의 다른 PC도 축을 움직일 수 있습니다.")
    if open_browser:
        import webbrowser
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        srv.server_close()
    return 0
