"""Minimal Modbus TCP/RTU client (FC01/03/05/06/15/16), no third-party library except optional pyserial for RTU.
TCP part copied from test-fw/host/tfw_host/modbus.py (read-only reuse)."""
import socket
import struct
import time


class ModbusError(Exception):
    pass


class ModbusException(ModbusError):
    def __init__(self, fc, code):
        super().__init__(f"exception FC{fc:02d} code {code:02d}")
        self.fc, self.code = fc, code


class ModbusTimeout(ModbusError):
    pass


class ModbusTcpClient:
    def __init__(self, host: str, port: int = 502, unit: int = 1, timeout: float = 2.0):
        self.host, self.port, self.unit, self.timeout = host, port, unit, timeout
        self.sock = None
        self.tid = 0
        self.last_rtt_us = 0.0

    def connect(self):
        self.sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.sock.settimeout(self.timeout)
        return self

    def close(self):
        if self.sock:
            try:
                self.sock.close()
            finally:
                self.sock = None

    def __enter__(self):
        return self.connect()

    def __exit__(self, *a):
        self.close()

    def _recv(self, n):
        buf = b""
        while len(buf) < n:
            try:
                chunk = self.sock.recv(n - len(buf))
            except socket.timeout as e:
                raise ModbusTimeout("timeout") from e
            if not chunk:
                raise ModbusError("connection closed")
            buf += chunk
        return buf

    def request(self, pdu: bytes) -> bytes:
        self.tid = (self.tid + 1) & 0xFFFF
        frame = struct.pack(">HHHB", self.tid, 0, len(pdu) + 1, self.unit) + pdu
        t0 = time.perf_counter()
        self.sock.sendall(frame)
        hdr = self._recv(7)
        tid, proto, length, unit = struct.unpack(">HHHB", hdr)
        body = self._recv(length - 1)
        self.last_rtt_us = (time.perf_counter() - t0) * 1e6
        if tid != self.tid or proto != 0:
            raise ModbusError("bad MBAP header")
        if body[0] & 0x80:
            raise ModbusException(body[0] & 0x7F, body[1])
        return body

    def read_holding(self, addr: int, qty: int):
        body = self.request(struct.pack(">BHH", 3, addr, qty))
        if body[1] != qty * 2:
            raise ModbusError("bad byte count")
        return list(struct.unpack(f">{qty}H", body[2:2 + qty * 2]))

    def write_register(self, addr: int, value: int):
        self.request(struct.pack(">BHH", 6, addr, value & 0xFFFF))

    def write_registers(self, addr: int, values):
        values = [v & 0xFFFF for v in values]
        self.request(struct.pack(">BHHB", 16, addr, len(values), len(values) * 2) + struct.pack(f">{len(values)}H", *values))

    def read_coils(self, addr: int, qty: int):
        body = self.request(struct.pack(">BHH", 1, addr, qty))
        return [bool(body[2 + i // 8] & (1 << (i % 8))) for i in range(qty)]

    def write_coil(self, addr: int, on: bool):
        self.request(struct.pack(">BHH", 5, addr, 0xFF00 if on else 0))


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


class ModbusRtuClient(ModbusTcpClient):
    """RTU over pyserial (optional dependency). Same method set as the TCP client (backup/restore/status only)."""

    def __init__(self, port: str, unit: int = 1, baud: int = 19200, parity: str = "E", timeout: float = 1.0):
        self.port, self.unit, self.baud, self.parity, self.timeout = port, unit, baud, parity, timeout
        self.ser = None
        self.last_rtt_us = 0.0

    def connect(self):
        try:
            import serial
        except ImportError as e:
            raise ModbusError("RTU needs pyserial: pip install pyserial") from e
        self.ser = serial.Serial(self.port, self.baud, bytesize=8, parity=self.parity, stopbits=1, timeout=self.timeout)
        return self

    def close(self):
        if self.ser:
            self.ser.close()
            self.ser = None

    def _read_exact(self, n):
        buf = self.ser.read(n)
        if len(buf) != n:
            raise ModbusTimeout("timeout")
        return buf

    def request(self, pdu: bytes) -> bytes:
        frame = bytes([self.unit]) + pdu
        frame += struct.pack("<H", crc16(frame))
        self.ser.reset_input_buffer()
        self.ser.write(frame)
        head = self._read_exact(2)
        fc = head[1]
        if fc & 0x80:
            rest = self._read_exact(3)
            body = head[1:] + rest[:1]
            crc_rx = rest[1:]
            full = head + rest[:1]
        else:
            if fc in (1, 2, 3, 4):
                cnt = self._read_exact(1)
                data = self._read_exact(cnt[0] + 2)
                full = head + cnt + data[:-2]
                crc_rx = data[-2:]
                body = full[1:]
            else:
                data = self._read_exact(6)
                full = head + data[:-2]
                crc_rx = data[-2:]
                body = full[1:]
        if struct.pack("<H", crc16(full)) != crc_rx:
            raise ModbusError("bad CRC")
        if body[0] & 0x80:
            raise ModbusException(body[0] & 0x7F, body[1])
        return body
