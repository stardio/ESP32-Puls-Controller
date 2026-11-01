"""Unit conversion (FR-029). User units are scaled x0.001 (um or m-deg; for pulse mode: milli-pulse [REC])."""
from .regmap import AXIS_BASE  # noqa: F401  (kept so units depends only on regmap)

MAX_PPS = 1_000_000


def _rdiv(n: int, d: int) -> int:
    """Integer division, round half away from zero."""
    if d < 0:
        n, d = -n, -d
    return (2 * n + d) // (2 * d) if n >= 0 else -((2 * -n + d) // (2 * d))


def pulses_per_unit_num_den(unit_mode, ppr, lead_um, gear_num, gear_den):
    """pulses = value_milli * num / den. Returns (num, den)."""
    if unit_mode == 0:
        return 1, 1000
    if unit_mode == 1:     # mm: value in um
        return ppr * gear_num, gear_den * lead_um * 1
    if unit_mode == 2:     # deg: value in mdeg
        return ppr * gear_num, gear_den * 360_000
    raise ValueError("unit_mode")


def to_pulses(value_milli, unit_mode, ppr, lead_um, gear_num, gear_den) -> int:
    num, den = pulses_per_unit_num_den(unit_mode, ppr, lead_um, gear_num, gear_den)
    return _rdiv(value_milli * num, den)


def to_units(pulses, unit_mode, ppr, lead_um, gear_num, gear_den) -> int:
    num, den = pulses_per_unit_num_den(unit_mode, ppr, lead_um, gear_num, gear_den)
    return _rdiv(pulses * den, num)


def speed_to_pps(speed_milli, unit_mode, ppr, lead_um, gear_num, gear_den) -> int:
    return to_pulses(speed_milli, unit_mode, ppr, lead_um, gear_num, gear_den)


def rpm_to_pps(rpm, ppr) -> int:
    return _rdiv(rpm * ppr, 60)


def max_rpm(max_speed, ppr) -> int:
    return min(MAX_PPS, max_speed) * 60 // ppr
