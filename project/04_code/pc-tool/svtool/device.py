"""High-level device access on top of a Modbus client."""
import time
from . import regmap as R
from .modbus import ModbusRtuClient, ModbusTcpClient, ModbusException


def open_target(target: str, unit=1, port=502, baud=19200, parity="E", timeout=2.0):
    t = target.strip()
    if t.upper().startswith("COM") or t.startswith("/dev/"):
        return ModbusRtuClient(t, unit=unit, baud=baud, parity=parity, timeout=timeout).connect()
    host = t
    if ":" in t:
        host, p = t.rsplit(":", 1)
        port = int(p)
    return ModbusTcpClient(host, port, unit, timeout).connect()


class Device:
    def __init__(self, client):
        self.c = client

    def read(self, field: R.Field):
        return R.decode(field.kind, self.c.read_holding(field.addr, field.words))

    def read_words(self, addr, n):
        return self.c.read_holding(addr, n)

    def write(self, field: R.Field, value: int):
        self.c.write_registers(field.addr, R.encode(field.kind, value))

    def identity(self):
        w = self.c.read_holding(R.MODEL_ID, 2)
        fwraw = self.c.read_holding(R.FW_VERSION, 2)
        raw = fwraw[0] | (fwraw[1] << 16)
        rv = self.c.read_holding(R.REGMAP_VER, 1)[0]
        return dict(model_id=w[0], axis_count=w[1], fw_version_raw=raw, fw_version=R.fw_version_str(raw),
                    regmap_version=rv)

    def unlock(self):
        self.c.write_register(R.UNLOCK_KEY_ADDR, R.UNLOCK_KEY)

    def sys_command(self, cmd: int, timeout=3.0):
        self.c.write_register(R.SYS_COMMAND, cmd)
        t0 = time.monotonic()
        while True:
            res = self.c.read_holding(R.SYS_CMD_RESULT, 1)[0]
            if res != R.RESULT_BUSY or time.monotonic() - t0 > timeout:
                return res
            time.sleep(0.05)
