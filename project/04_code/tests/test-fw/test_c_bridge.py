"""Python client <-> the real firmware PDU engine (tfw_logic compiled with gcc, called through ctypes)."""
import ctypes
import os
import socketserver
import subprocess
import threading

import pytest

from conftest import ROOT, HERE
from tfw_host import regs
from tfw_host.modbus import ModbusTcpClient, ModbusException

L = os.path.join(ROOT, "components", "tfw_logic")


@pytest.fixture(scope="module")
def lib(tmp_path_factory):
    so = str(tmp_path_factory.mktemp("bridge") / "libbridge.so")
    subprocess.check_call(["gcc", "-shared", "-fPIC", "-O1", "-std=c11", f"-I{L}/include", "-o", so,
                           os.path.join(HERE, "bridge.c"), f"{L}/tfw_core.c", f"{L}/tfw_regwin.c",
                           f"{L}/tfw_seq.c", f"{L}/tfw_mb.c", "-lm"])
    lb = ctypes.CDLL(so)
    lb.bridge_process.argtypes = [ctypes.c_char_p, ctypes.c_uint, ctypes.c_char_p]
    lb.bridge_process.restype = ctypes.c_uint
    lb.bridge_status.restype = ctypes.c_uint
    lb.bridge_take_cmd.restype = ctypes.c_uint
    lb.bridge_set_seq_done.argtypes = [ctypes.c_uint]
    return lb


class _H(socketserver.BaseRequestHandler):
    def handle(self):
        lb = self.server.lib
        while True:
            hdr = self._r(7)
            if hdr is None:
                return
            ln = int.from_bytes(hdr[4:6], "big")
            pdu = self._r(ln - 1)
            if pdu is None:
                return
            buf = ctypes.create_string_buffer(300)
            with self.server.lk:
                n = lb.bridge_process(pdu, len(pdu), buf)
            self.request.sendall(hdr[:4] + (n + 1).to_bytes(2, "big") + hdr[6:7] + buf.raw[:n])

    def _r(self, n):
        b = b""
        while len(b) < n:
            c = self.request.recv(n - len(b))
            if not c:
                return None
            b += c
        return b


@pytest.fixture()
def cli(lib):
    lib.bridge_reset()
    socketserver.ThreadingTCPServer.allow_reuse_address = True
    srv = socketserver.ThreadingTCPServer(("127.0.0.1", 0), _H)
    srv.lib, srv.lk = lib, threading.Lock()
    srv.daemon_threads = True
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    c = ModbusTcpClient("127.0.0.1", srv.server_address[1]).connect()
    yield c
    c.close()
    srv.shutdown()
    srv.server_close()


def test_window_roundtrip_matches_python_register_map(cli, lib):
    cli.write_registers(regs.R["FREQ"], regs.split_u32(750000))
    cli.write_registers(regs.R["COUNT"], regs.split_u32(4294967295))
    cli.write_register(regs.R["AXIS_MASK"], 3)
    lib.bridge_set_seq_done(1000)
    lo, hi = cli.read_holding(regs.R["FREQ"], 2)
    assert regs.join_u32(lo, hi) == 750000
    lo, hi = cli.read_holding(regs.R["COUNT"], 2)
    assert regs.join_u32(lo, hi) == 4294967295
    lo, hi = cli.read_holding(regs.R["SEQ_DONE"], 2)
    assert regs.join_u32(lo, hi) == 1000
    assert cli.read_holding(regs.R["CHIP_REV"], 2) == [301, 400]


def test_range_error_sets_status_0x11_and_command_reaches_firmware(cli, lib):
    cli.write_register(regs.R["AXIS_MASK"], 9)
    assert cli.read_holding(regs.R["STATUS"], 1)[0] == regs.STATUS["BAD_VALUE"]
    cli.write_register(regs.R["CMD"], regs.CMD["RUN_SEQ"])
    assert lib.bridge_take_cmd() == regs.CMD["RUN_SEQ"]
    assert cli.read_holding(regs.R["CMD"], 1)[0] == 0


def test_125_register_read_and_exceptions(cli):
    assert len(cli.read_holding(0x7000, 125)) == 125
    with pytest.raises(ModbusException) as e:
        cli.write_register(regs.R["SEQ_DONE"], 1)
    assert e.value.code == 2
    with pytest.raises(ModbusException) as e:
        cli.write_register(regs.R["FREQ"], 1)                # half of a 32-bit item
    assert e.value.code == 3
    with pytest.raises(ModbusException):
        cli.read_holding(0x70F0, 20)


def test_legacy_window_for_existing_hmi(cli):
    pos = cli.read_holding(0, 2)
    assert regs.join_u32(*pos) == 5000
    cli.write_registers(2, regs.split_u32(250000))           # speed
    assert regs.join_u32(*cli.read_holding(2, 2)) == 250000
    assert regs.join_u32(*cli.read_holding(18, 2)) == 5000    # automatic start speed
    cli.write_coil(0, True)
    assert cli.read_coils(0, 4)[0] is True
