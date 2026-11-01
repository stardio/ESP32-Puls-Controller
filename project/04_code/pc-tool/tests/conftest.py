import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from svtool.sim import Sim, SimServer  # noqa: E402
from svtool.modbus import ModbusTcpClient  # noqa: E402
from svtool.device import Device  # noqa: E402


class FakeClock:
    def __init__(self):
        self.t = 100.0

    def __call__(self):
        return self.t

    def advance(self, s):
        self.t += s


@pytest.fixture
def sim():
    return Sim(2, clock=FakeClock())


@pytest.fixture
def server(sim):
    srv = SimServer(sim, "127.0.0.1", 0)
    srv.start()
    yield srv
    srv.shutdown()
    srv.server_close()


@pytest.fixture
def target(server):
    return f"127.0.0.1:{server.server_address[1]}"


@pytest.fixture
def dev(target):
    c = ModbusTcpClient("127.0.0.1", int(target.split(":")[1])).connect()
    yield Device(c)
    c.close()
