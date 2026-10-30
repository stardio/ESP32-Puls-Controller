"""C-02: the host reproduction of the sequencer targets equals what the REAL tfw_seq_run (C, gcc) asks the hardware for."""
import ctypes
import os
import subprocess

import pytest

from conftest import ROOT, HERE
from tfw_host.expect import expected_edges, rand_next

L = os.path.join(ROOT, "components", "tfw_logic")


@pytest.fixture(scope="module")
def lib(tmp_path_factory):
    so = str(tmp_path_factory.mktemp("bridge2") / "libbridge2.so")
    subprocess.check_call(["gcc", "-shared", "-fPIC", "-O1", "-std=c11", f"-I{L}/include", "-o", so,
                           os.path.join(HERE, "bridge.c"), f"{L}/tfw_core.c", f"{L}/tfw_regwin.c",
                           f"{L}/tfw_seq.c", f"{L}/tfw_mb.c", "-lm"])
    lb = ctypes.CDLL(so)
    lb.bridge_seq_targets.argtypes = [ctypes.c_uint] * 6 + [ctypes.POINTER(ctypes.c_ulonglong)]
    return lb


def c_targets(lb, cmd, mask, freq, count, seed, repeat):
    out = (ctypes.c_ulonglong * 2)()
    lb.bridge_seq_targets({"RUN_FIXED": 1, "RUN_MOVE": 2, "RUN_SEQ": 3}[cmd], mask, freq, count, seed, repeat, out)
    return [out[0], out[1]]


@pytest.mark.parametrize("cmd,mask,freq,count,seed,repeat", [
    ("RUN_SEQ", 3, 200000, 1000, 12345, 1000),       # random, two axes (the T-04 case)
    ("RUN_SEQ", 3, 1000000, 1000, 777, 300),
    ("RUN_SEQ", 1, 200000, 1000, 99, 200),           # axis 2 masked out: only axis 1 draws numbers
    ("RUN_SEQ", 2, 200000, 1000, 99, 200),
    ("RUN_SEQ", 3, 200000, 5, 0, 50),                # seed 0 -> fixed count each iteration
    ("RUN_MOVE", 3, 200000, 777, 5, 9),              # not RUN_SEQ: one iteration, seed ignored
    ("RUN_FIXED", 2, 200000, 31, 5, 9),
])
def test_host_expected_edges_equal_c_sequencer(lib, cmd, mask, freq, count, seed, repeat):
    step = {"cmd": cmd, "axis_mask": mask, "freq_hz": freq, "count": count, "seed": seed, "repeat": repeat}
    assert expected_edges(step) == c_targets(lib, cmd, mask, freq, count, seed, repeat)


def test_random_axes_really_differ_so_a_single_expected_value_is_wrong():
    """The old judge compared both axes with ONE number. With a random sequence the two axes get different sums."""
    e = expected_edges({"cmd": "RUN_SEQ", "axis_mask": 3, "freq_hz": 200000, "seed": 12345, "repeat": 1000})
    assert e[0] != e[1] and e[0] > 0 and e[1] > 0


def test_xorshift_known_values():
    s, v = rand_next(1)
    assert v == 270369                       # hand-computed: 1 -> 8193 -> 8193 -> 8193 ^ (8193 << 5)
    s0, v0 = rand_next(0)                    # state 0 is replaced by the constant, like the C code
    assert v0 == rand_next(0x9E3779B9)[1]
