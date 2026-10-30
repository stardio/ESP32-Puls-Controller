"""Local mock of the TFW Modbus server (test aid for load_clients / run_scenario; NOT the firmware).
Implements FC03/06/16 on a flat register dict, the 4-client limit, and a trivial RUN_* simulation."""
import socketserver
import struct
import threading
import time

from . import regs


class _Handler(socketserver.BaseRequestHandler):
    def handle(self):
        srv = self.server.owner
        if not srv._admit():
            return
        try:
            self.request.settimeout(5)
            while True:
                hdr = self._recv(7)
                if hdr is None:
                    return
                tid, proto, length, unit = struct.unpack(">HHHB", hdr)
                pdu = self._recv(length - 1)
                if pdu is None:
                    return
                resp = srv.process(pdu)
                if srv.delay_s:
                    time.sleep(srv.delay_s)
                self.request.sendall(struct.pack(">HHHB", tid, 0, len(resp) + 1, unit) + resp)
        except OSError:
            pass
        finally:
            srv._leave()

    def _recv(self, n):
        buf = b""
        while len(buf) < n:
            try:
                c = self.request.recv(n - len(buf))
            except OSError:
                return None
            if not c:
                return None
            buf += c
        return buf


class MockTfwServer:
    def __init__(self, max_clients=4, delay_s=0.0, run_time_s=0.05):
        self.regs = {}
        self.max_clients = max_clients
        self.delay_s = delay_s
        self.run_time_s = run_time_s
        self.clients = 0
        self.rejected = 0
        self.requests = 0
        self.lock = threading.Lock()
        socketserver.ThreadingTCPServer.allow_reuse_address = True
        self.server = socketserver.ThreadingTCPServer(("127.0.0.1", 0), _Handler)
        self.server.daemon_threads = True
        self.server.owner = self
        self.port = self.server.server_address[1]
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.regs[regs.R["CHIP_REV"]] = 301
        self.regs[regs.R["CPU_MHZ"]] = 400

    def start(self):
        self.thread.start()
        return self

    def stop(self):
        self.server.shutdown()
        self.server.server_close()

    def _admit(self):
        with self.lock:
            if self.clients >= self.max_clients:
                self.rejected += 1
                return False
            self.clients += 1
            return True

    def _leave(self):
        with self.lock:
            self.clients -= 1

    def _set32(self, name, v):
        a = regs.R[name]
        self.regs[a], self.regs[a + 1] = regs.split_u32(v)

    def _get32(self, name):
        a = regs.R[name]
        return regs.join_u32(self.regs.get(a, 0), self.regs.get(a + 1, 0))

    def _run(self, cmd):
        """Simulated sequencer: finishes after run_time_s with error-free counters."""
        R = regs.R
        self.regs[R["STATUS"]] = regs.STATUS["RUNNING"]
        mask = self.regs.get(R["AXIS_MASK"], 1)
        repeat = self._get32("REPEAT") if cmd == regs.CMD["RUN_SEQ"] else 1
        count = self._get32("COUNT")

        def finish():
            time.sleep(self.run_time_s)
            with self.lock:
                self._set32("SEQ_DONE", self._get32("SEQ_DONE") + repeat)
                for i, ax in enumerate(("AX1", "AX2")):
                    if mask & (1 << i):
                        self._set32(f"LAST_TGT_{ax}", count)
                        self._set32(f"LAST_CNT_{ax}", count)
                self.regs[R["STATUS"]] = regs.STATUS["DONE"]
        threading.Thread(target=finish, daemon=True).start()

    def process(self, pdu):
        fc = pdu[0]
        self.requests += 1
        if fc == 3:
            a, n = struct.unpack(">HH", pdu[1:5])
            vals = [self.regs.get(a + i, 0) & 0xFFFF for i in range(n)]
            return struct.pack(">BB", 3, n * 2) + struct.pack(f">{n}H", *vals)
        if fc == 6:
            a, v = struct.unpack(">HH", pdu[1:5])
            self._write(a, [v])
            return pdu[:5]
        if fc == 16:
            a, n, bc = struct.unpack(">HHB", pdu[1:6])
            vals = list(struct.unpack(f">{n}H", pdu[6:6 + n * 2]))
            self._write(a, vals)
            return struct.pack(">BHH", 16, a, n)
        return bytes([fc | 0x80, 1])

    def _write(self, a, vals):
        with self.lock:
            for i, v in enumerate(vals):
                self.regs[a + i] = v
            if a == regs.R["CMD"] and vals and vals[0] in (1, 2, 3):
                self.regs[a] = 0
                self._run(vals[0])
            elif a == regs.R["CMD"]:
                if vals and vals[0] == regs.CMD["CLEAR_STATS"]:
                    for n in ("SEQ_DONE", "ERR_AX1", "ERR_AX2", "OVS_AX1", "OVS_AX2"):
                        self._set32(n, 0)
                self.regs[a] = 0
