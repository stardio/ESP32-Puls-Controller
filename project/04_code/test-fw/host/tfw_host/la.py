"""Logic analyser CSV evaluation (TFW-05). Assumed export format [assumption]: header row, first column = time in
seconds, further columns = one digital channel each, a row written whenever any channel changes
(Saleae 'Time [s], Channel 0, ...' style). Channel is selected by header name."""
import csv
import statistics
from dataclasses import dataclass


@dataclass
class FreqStats:
    edges: int
    periods: int
    f_min: float
    f_avg: float
    f_max: float
    period_jitter_pct: float   # (max period - min period) / avg period * 100


@dataclass
class LatStats:
    triggers: int
    lat_us: list
    pulses_after: list
    lat_min_us: float
    lat_avg_us: float
    lat_max_us: float
    lat_p99_us: float


def _read(la_csv):
    with open(la_csv, newline="") as f:
        rd = csv.reader(f)
        header = [h.strip() for h in next(rd)]
        rows = [[float(r[0])] + [int(float(x)) for x in r[1:]] for r in rd if r and r[0].strip()]
    return header, rows


def _channel_index(header, channel):
    for i, h in enumerate(header):
        if h == channel:
            return i
    raise KeyError(f"channel {channel!r} not in {header}")


def _edges(la_csv, channel, kind):
    header, rows = _read(la_csv)
    ci = _channel_index(header, channel)
    out, prev = [], None
    for r in rows:
        v = r[ci]
        if prev is not None and v != prev:
            if kind == "any" or (kind == "rise" and v > prev) or (kind == "fall" and v < prev):
                out.append(r[0])
        prev = v
    return out


def count_edges(la_csv: str, channel: str, kind: str = "rise") -> int:
    """Number of rising (default), falling or 'any' edges. The first row is the initial state, not an edge."""
    return len(_edges(la_csv, channel, kind))


def freq_stats(la_csv: str, channel: str) -> FreqStats:
    t = _edges(la_csv, channel, "rise")
    if len(t) < 2:
        return FreqStats(len(t), 0, 0.0, 0.0, 0.0, 0.0)
    per = [b - a for a, b in zip(t, t[1:])]
    avg_p = statistics.fmean(per)
    return FreqStats(len(t), len(per), 1.0 / max(per), 1.0 / avg_p, 1.0 / min(per),
                     (max(per) - min(per)) / avg_p * 100.0)


def latency(la_csv: str, trigger_ch: str, pulse_ch: str, trigger_edge: str = "fall",
            gap_s: float = 1e-3, window_s: float = 0.05) -> LatStats:
    """For every trigger edge: time from the trigger to the last pulse edge that belongs to the burst that was
    running at the trigger (pulse edges closer than gap_s to each other, within window_s)."""
    trig = _edges(la_csv, trigger_ch, trigger_edge)
    pul = _edges(la_csv, pulse_ch, "any")
    lat, after = [], []
    for i, t0 in enumerate(trig):
        t_end = min(t0 + window_s, trig[i + 1] if i + 1 < len(trig) else float("inf"))
        last, n_rise = None, 0
        prev = t0
        for p in pul:
            if p < t0:
                continue
            if p > t_end or p - prev > gap_s:
                break
            last = p
            prev = p
            n_rise += 1
        lat.append(((last - t0) * 1e6) if last is not None else 0.0)
        after.append((n_rise + 1) // 2)           # rising edges ~ half of all edges after the trigger
    if not lat:
        return LatStats(0, [], [], 0.0, 0.0, 0.0, 0.0)
    s = sorted(lat)
    p99 = s[min(len(s) - 1, int(round(0.99 * (len(s) - 1))))]
    return LatStats(len(trig), lat, after, min(lat), statistics.fmean(lat), max(lat), p99)
