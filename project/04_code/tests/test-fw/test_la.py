"""TFW-05 criterion 1: count_edges / freq_stats exact on synthetic CSV; latency on synthetic burst."""
import pytest

from tfw_host.la import count_edges, freq_stats, latency


def write_pulses(path, n, f_hz, t0=0.0, dir_ch=False, jitter=None):
    """Saleae-like: Time [s],ax1,trig. Pulse = high for half period."""
    per = 1.0 / f_hz
    rows = [(0.0, 0, 1)]
    t = t0
    for i in range(n):
        p = per if jitter is None else per + jitter[i % len(jitter)]
        rows.append((t, 1, 1))
        rows.append((t + p / 2, 0, 1))
        t += p
    with open(path, "w") as f:
        f.write("Time [s],ax1,trig\n")
        for r in rows:
            f.write(f"{r[0]:.12f},{r[1]},{r[2]}\n")


@pytest.mark.parametrize("n,f", [(1, 200000), (2, 200000), (1000, 200000), (30001, 500000), (1000000, 1000000)])
def test_count_edges_exact(tmp_path, n, f):
    p = tmp_path / "la.csv"
    write_pulses(p, n, f)
    assert count_edges(str(p), "ax1") == n
    assert count_edges(str(p), "ax1", "fall") == n
    assert count_edges(str(p), "ax1", "any") == 2 * n


@pytest.mark.parametrize("f", [100, 200000, 285714, 1000000])
def test_freq_stats_constant(tmp_path, f):
    p = tmp_path / "la.csv"
    write_pulses(p, 500, f)
    s = freq_stats(str(p), "ax1")
    assert s.edges == 500 and s.periods == 499
    assert s.f_avg == pytest.approx(f, rel=1e-6)
    assert s.f_min == pytest.approx(f, rel=1e-6) and s.f_max == pytest.approx(f, rel=1e-6)
    assert s.period_jitter_pct < 1e-3


def test_freq_stats_with_jitter(tmp_path):
    p = tmp_path / "la.csv"
    write_pulses(p, 400, 100000, jitter=[0.0, 1e-6])           # periods 10 us and 11 us
    s = freq_stats(str(p), "ax1")
    assert s.f_max == pytest.approx(100000, rel=1e-6)
    assert s.f_min == pytest.approx(1 / 11e-6, rel=1e-6)
    assert s.period_jitter_pct == pytest.approx(1.0 / 10.5 * 100, rel=1e-3)


def test_freq_stats_too_few_edges(tmp_path):
    p = tmp_path / "la.csv"
    write_pulses(p, 1, 1000)
    s = freq_stats(str(p), "ax1")
    assert s.periods == 0 and s.f_avg == 0.0


def test_unknown_channel(tmp_path):
    p = tmp_path / "la.csv"
    write_pulses(p, 3, 1000)
    with pytest.raises(KeyError):
        count_edges(str(p), "nope")


def test_latency_burst(tmp_path):
    # 100 kHz burst; trigger falls at t=1 ms; pulses continue until 1 ms + 37 us then stop
    p = tmp_path / "la.csv"
    rows = [(0.0, 0, 1)]
    t = 0.0
    while t < 1e-3 + 37e-6:
        rows.append((t, 1, 1)); rows.append((t + 5e-6, 0, 1)); t += 10e-6
        if abs(t - 1e-3) < 1e-9:
            rows.append((t, 0, 0))                                # trigger falling edge
    rows.sort(key=lambda r: r[0])
    with open(p, "w") as f:
        f.write("Time [s],ax1,trig\n")
        for r in rows:
            f.write(f"{r[0]:.12f},{r[1]},{r[2]}\n")
    ls = latency(str(p), "trig", "ax1")
    assert ls.triggers == 1
    # last edge is the falling edge of the pulse that started at 1.030 ms -> 1.035 ms ; t_trig = 1.000 ms
    assert ls.lat_max_us == pytest.approx(35.0, abs=0.5)
    assert ls.pulses_after[0] >= 3
