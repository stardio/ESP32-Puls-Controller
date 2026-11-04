"""svtool web: HTTP API against the simulator (FakeClock), plus the request guards."""
import json
import os
import re
import threading
import urllib.error
import urllib.request

import pytest

from svtool import regmap as R
from svtool.web import Bridge, make_server


@pytest.fixture
def web(target):
    srv = make_server(Bridge(target), "127.0.0.1", 0)
    t = threading.Thread(target=srv.serve_forever, daemon=True)
    t.start()
    yield f"http://127.0.0.1:{srv.server_address[1]}"
    srv.shutdown()
    srv.server_close()


def get(url, headers=None):
    with urllib.request.urlopen(urllib.request.Request(url, headers=headers or {})) as r:
        return r.status, r.headers, r.read()


def post(url, body, headers=None):
    h = {"Content-Type": "application/json", "X-Svtool": "1"}
    h.update(headers or {})
    req = urllib.request.Request(url, json.dumps(body).encode(), h, method="POST")
    try:
        with urllib.request.urlopen(req) as r:
            return r.status, json.loads(r.read())
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read())


def state(web):
    return json.loads(get(web + "/api/state")[2])


def test_page_served(web):
    code, hdr, body = get(web + "/")
    assert code == 200 and hdr["Content-Type"].startswith("text/html")
    assert "펄스 서보 컨트롤러" in body.decode("utf-8")


def test_state_identity_and_axes(web):
    s = state(web)
    assert s["connected"] and s["regmap_ok"] and s["axis_count"] == 2
    assert [a["name"] for a in s["axes"]] == ["X", "Y"]
    assert s["axes"][0]["state_text"] and "items" in s["wiring"]


def test_post_guards(web):
    assert post(web + "/api/stop_all", {}, {"X-Svtool": ""})[0] == 403          # missing custom header
    assert post(web + "/api/stop_all", {}, {"Content-Type": "text/plain"})[0] == 403
    assert post(web + "/api/stop_all", {}, {"Host": "evil.example:8080"})[0] == 403   # DNS rebinding
    with pytest.raises(urllib.error.HTTPError) as e:
        get(web + "/api/state", {"Host": "evil.example"})
    assert e.value.code == 403


def test_unknown_axis_command_refused(web):
    code, r = post(web + "/api/axis", {"axis": 1, "cmd": "POINT_TEACH"})
    assert code == 200 and not r["ok"]


def test_relative_move(web, sim):
    code, r = post(web + "/api/axis", {"axis": 1, "cmd": "START_REL",
                                       "operands": {"target": 5000, "speed": 10000, "accel_ms": 10, "decel_ms": 10}})
    assert r["ok"], r
    sim.clock.advance(2.0)
    a = state(web)["axes"][0]
    assert a["position"] == 5000 and not a["busy"]


def test_position_reset_to_zero(web, sim):
    post(web + "/api/axis", {"axis": 1, "cmd": "START_REL", "operands": {"target": 3000, "speed": 10000}})
    _, r = post(web + "/api/axis", {"axis": 1, "cmd": "SET_POSITION", "operands": {"target": 0}})
    assert not r["ok"]                                   # refused while the axis is moving
    sim.clock.advance(2.0)
    assert state(web)["axes"][0]["position"] == 3000
    _, r = post(web + "/api/axis", {"axis": 1, "cmd": "SET_POSITION", "operands": {"target": 0}})
    assert r["ok"], r
    assert state(web)["axes"][0]["position"] == 0


def test_operand_range_checked(web):
    _, r = post(web + "/api/axis", {"axis": 1, "cmd": "START_REL", "operands": {"target": 1, "speed": 10, "accel_ms": 5}})
    assert not r["ok"] and "accel_ms" in r["text"]


def test_hold_to_run_stops_without_keepalive(web, sim):
    _, r = post(web + "/api/axis", {"axis": 2, "cmd": "TEST_POS"})
    assert r["ok"], r
    sim.clock.advance(0.2)
    assert state(web)["axes"][1]["busy"]
    sim.clock.advance(2.0)                       # no keepalive -> simulator decel-stops after 500 ms
    assert not state(web)["axes"][1]["busy"]


def test_params_read_write_and_save(web):
    params = json.loads(get(web + "/api/params?axis=1")[2])["params"]
    assert {p["name"] for p in params} >= {"max_speed", "test_rpm"}
    _, r = post(web + "/api/params", {"axis": 1, "values": {"test_rpm": 999}})
    assert not r["ok"] and "범위" in r["errors"][0]
    _, r = post(web + "/api/params", {"axis": 1, "values": {"test_rpm": 120}})
    assert r["ok"] and r["written"] == 1
    params = json.loads(get(web + "/api/params?axis=1")[2])["params"]
    assert next(p["value"] for p in params if p["name"] == "test_rpm") == 120
    assert state(web)["unsaved"]
    _, r = post(web + "/api/save", {})
    assert r["ok"]
    assert not state(web)["unsaved"]


def test_stop_all(web):
    post(web + "/api/axis", {"axis": 1, "cmd": "START_REL", "operands": {"target": 10 ** 6, "speed": 1000}})
    _, r = post(web + "/api/stop_all", {})
    assert r["ok"], r


def test_backup_download(web):
    code, hdr, body = get(web + "/api/backup")
    assert code == 200 and "attachment" in hdr["Content-Disposition"]
    data = json.loads(body)
    assert data["format"] == "svtool-backup" and data["regmap_version"] == R.REGMAP_VERSION


def kconfig_gpio_defaults():
    """GPIO defaults from the product firmware Kconfig (the source of truth for the HELP pin table)."""
    path = os.path.join(os.path.dirname(__file__), "..", "..", "firmware", "main", "Kconfig.projbuild")
    text = open(path, encoding="utf-8").read()
    return {name: int(v) for name, v in re.findall(r"config (FW_\w+_GPIO)\s+int [^\n]*\n\s+default (-?\d+)", text)}


def test_help_pins_match_firmware_kconfig(web):
    pins = json.loads(get(web + "/api/help")[2])["pins"]
    table = {p["kconfig"]: p["gpio"] for p in pins if p["kconfig"]}
    assert table == kconfig_gpio_defaults()                 # every firmware GPIO listed, none stale
    gpios = [p["gpio"] for p in pins]
    assert len(gpios) == len(set(gpios))


def test_device_down_reports_503():
    srv = make_server(Bridge("127.0.0.1:1", timeout=0.3), "127.0.0.1", 0)
    t = threading.Thread(target=srv.serve_forever, daemon=True)
    t.start()
    try:
        with pytest.raises(urllib.error.HTTPError) as e:
            get(f"http://127.0.0.1:{srv.server_address[1]}/api/state")
        assert e.value.code == 503
    finally:
        srv.shutdown()
        srv.server_close()
