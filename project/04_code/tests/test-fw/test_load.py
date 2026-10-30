"""TFW-05 criterion 2: load_clients with 4 connections against the local mock Modbus server."""
import socket
import time

from tfw_host.load import load_clients, start_load
from tfw_host.mock import MockTfwServer
from tfw_host.modbus import ModbusTcpClient
from tfw_host import regs


def test_four_clients_fc03_125_registers():
    srv = MockTfwServer().start()
    try:
        st = load_clients(f"127.0.0.1:{srv.port}", 4, "FC03", 125, 0, 1.0)
        assert st.clients == 4
        assert st.requests > 100 and st.errors == 0 and st.refused == 0
        assert len(st.latencies_us) == st.requests
        s = st.summary()
        assert sum(s["hist"]) == st.requests
        assert s["rtt_us_min"] <= s["rtt_us_avg"] <= s["rtt_us_max"]
        assert s["rtt_us_p99"] <= s["rtt_us_max"]
        assert abs(s["duration_s"] - 1.0) < 0.5
        assert srv.requests >= st.requests
    finally:
        srv.stop()


def test_mixed_fc03_fc16_writes_land():
    srv = MockTfwServer().start()
    try:
        st = load_clients(f"127.0.0.1:{srv.port}", 4, "MIX", 125, 1.0, 0.5)
        assert st.errors == 0 and st.requests > 20
        assert regs.R["SEED"] in srv.regs                    # FC16 reached the server
    finally:
        srv.stop()


def test_fifth_connection_refused_by_mock_limit():
    srv = MockTfwServer(max_clients=4).start()
    try:
        run = start_load(f"127.0.0.1:{srv.port}", 4, "FC03", 10, 5.0)
        time.sleep(0.3)
        c5 = ModbusTcpClient("127.0.0.1", srv.port, timeout=1.0).connect()
        try:
            try:
                c5.read_holding(0x7000, 1)
                refused = False
            except Exception:
                refused = True
        finally:
            c5.close()
        run.stop()
        assert refused and srv.rejected >= 1
    finally:
        srv.stop()


def test_percentile_and_histogram_bins():
    from tfw_host.load import LoadStats
    s = LoadStats(latencies_us=[10, 49, 50, 99, 100, 499, 500, 999, 1000, 1999, 2000, 4999, 5000, 9e9])
    assert s.histogram() == [2, 2, 1, 1, 2, 2, 2, 2]
    assert s.percentile(0) == 10 and s.percentile(100) == 9e9
