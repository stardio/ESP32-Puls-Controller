"""Modbus load generation (TFW-05). Each client runs in its own thread with its own socket so clients do not
block each other (design: separate thread/process)."""
import statistics
import threading
import time
from dataclasses import dataclass, field

from . import regs
from .modbus import ModbusTcpClient, ModbusError, ModbusException, ModbusTimeout


@dataclass
class LoadStats:
    clients: int = 0
    duration_s: float = 0.0
    requests: int = 0
    errors: int = 0
    timeouts: int = 0
    exceptions: int = 0
    refused: int = 0
    latencies_us: list = field(default_factory=list)

    def percentile(self, p: float) -> float:
        if not self.latencies_us:
            return 0.0
        s = sorted(self.latencies_us)
        k = min(len(s) - 1, max(0, int(round(p / 100.0 * (len(s) - 1)))))
        return s[k]

    def histogram(self):
        """8 bins like the firmware histogram: <50,<100,<200,<500,<1k,<2k,<5k,>=5k us (client-side RTT)."""
        h = [0] * 8
        for v in self.latencies_us:
            for i, lim in enumerate(regs.HIST_LIMITS_US):
                if v < lim:
                    h[i] += 1
                    break
            else:
                h[7] += 1
        return h

    def summary(self) -> dict:
        lat = self.latencies_us
        return {
            "clients": self.clients, "duration_s": round(self.duration_s, 3), "requests": self.requests,
            "errors": self.errors, "timeouts": self.timeouts, "exceptions": self.exceptions, "refused": self.refused,
            "rtt_us_min": min(lat) if lat else 0, "rtt_us_avg": statistics.fmean(lat) if lat else 0,
            "rtt_us_p99": self.percentile(99), "rtt_us_max": max(lat) if lat else 0, "hist": self.histogram(),
        }


def _worker(host, port, unit, fc, qty, interval_s, stop_evt, out, lock, base_addr, write_addr=None):
    st = LoadStats()
    try:
        cli = ModbusTcpClient(host, port, unit, timeout=2.0).connect()
    except OSError:
        st.errors += 1
        st.refused += 1
        with lock:
            out.append(st)
        return
    n = 0
    # M-07: FC16 writes the value the register ALREADY has (read once at start) back to the same place, so the load
    # can never change the SEED (or any other input) the sequencer reads at RUN time.
    wa = regs.R["SEED"] if write_addr is None else write_addr
    try:
        wvals = cli.read_holding(wa, 2)
    except ModbusTimeout:
        st.errors += 1
        st.timeouts += 1
        wvals = [0, 0]
    except (ModbusError, OSError):
        # The firmware accepts the TCP connection and then closes it when max_clients is reached: a closed connection on
        # the very first request is a refused client (C-03). connect() alone cannot show this.
        st.errors += 1
        st.refused += 1
        cli.close()
        with lock:
            out.append(st)
        return
    try:
        while not stop_evt.is_set():
            try:
                use16 = (fc == "FC16") or (fc == "MIX" and n % 2 == 1)
                if use16:
                    cli.write_registers(wa, wvals)
                else:
                    cli.read_holding(base_addr, qty)
                st.requests += 1
                st.latencies_us.append(cli.last_rtt_us)
            except ModbusTimeout:
                st.errors += 1
                st.timeouts += 1
            except ModbusException:
                st.errors += 1
                st.exceptions += 1
            except (ModbusError, OSError):
                st.errors += 1
                break
            n += 1
            if interval_s > 0:
                stop_evt.wait(interval_s)
    finally:
        cli.close()
        with lock:
            out.append(st)


class _Running:
    def __init__(self, threads, stop_evt, out, lock, n, t0):
        self.threads, self.stop_evt, self.out, self.lock, self.n, self.t0 = threads, stop_evt, out, lock, n, t0

    def stop(self) -> LoadStats:
        self.stop_evt.set()
        for t in self.threads:
            t.join(timeout=5)
        res = LoadStats(clients=self.n, duration_s=time.perf_counter() - self.t0)
        with self.lock:
            for s in self.out:
                res.requests += s.requests
                res.errors += s.errors
                res.timeouts += s.timeouts
                res.exceptions += s.exceptions
                res.refused += s.refused
                res.latencies_us.extend(s.latencies_us)
        return res


def start_load(target: str, n: int, fc: str = "FC03", qty: int = 125, interval_ms: float = 0.0, base_addr: int = 0x7000, unit: int = 1, write_addr=None):
    host, _, port = target.partition(":")
    port = int(port or 502)
    stop_evt, out, lock = threading.Event(), [], threading.Lock()
    threads = [threading.Thread(target=_worker, args=(host, port, unit, fc, qty, interval_ms / 1000.0, stop_evt, out, lock, base_addr), daemon=True)
               for _ in range(n)]
    t0 = time.perf_counter()
    for t in threads:
        t.start()
    return _Running(threads, stop_evt, out, lock, n, t0)


def load_clients(target: str, n: int, fc: str, qty: int, interval_ms: float, duration_s: float, base_addr: int = 0x7000, unit: int = 1) -> LoadStats:
    """n parallel clients, fc in {"FC03","FC16","MIX"}, for duration_s. Returns merged statistics."""
    run = start_load(target, n, fc, qty, interval_ms, base_addr, unit)
    time.sleep(duration_s)
    return run.stop()
