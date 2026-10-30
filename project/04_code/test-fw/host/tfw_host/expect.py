"""Host-side reproduction of the firmware sequencer targets (C-02). Mirrors tfw_core.c tfw_rand_* and tfw_seq.c
(the same xorshift32, the same consumption order). tests/test-fw/test_expect.py compares it with the real C code."""
M32 = 0xFFFFFFFF
DISTANCES = (1, 2, 1000, 29999, 30000, 30001, 1000000)


def rand_next(s: int):
    x = s if s else 0x9E3779B9
    x ^= (x << 13) & M32
    x ^= x >> 17
    x ^= (x << 5) & M32
    return x, x


def expected_edges(step: dict) -> list:
    """Pulses each axis must emit in total: [axis1, axis2]. Same rules as tfw_seq_run: RUN_SEQ with seed != 0 draws a
    new distance (and 3 motion values, which are consumed but do not change the count) per axis and iteration."""
    cmd = step.get("cmd", "RUN_SEQ")
    mask = step.get("axis_mask", 3)
    count = step.get("count", 1000)
    freq = step.get("freq_hz", 100000)
    seed = step.get("seed", 0) & M32
    iterations = step.get("repeat", 1) if cmd == "RUN_SEQ" else 1
    random_mode = cmd == "RUN_SEQ" and seed != 0
    totals = [0, 0]
    rng = seed
    for _ in range(iterations):
        for ax in range(2):
            if not (mask & (1 << ax)):
                continue
            c = count
            if random_mode:
                rng, r = rand_next(rng)
                c = DISTANCES[r % len(DISTANCES)]
                lo = max(freq // 10, 100)
                lo = min(lo, freq)
                rng, _ = rand_next(rng)          # speed
                rng, _ = rand_next(rng)          # accel
                rng, _ = rand_next(rng)          # decel
            totals[ax] += c
    return totals
