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


def test_sync_start_both_axes(web, sim):
    ops = {"1": {"target": 1000, "speed": 10000}, "2": {"target": -2000, "speed": 10000}}
    _, r = post(web + "/api/sync", {"cmd": "START", "mode": "REL", "operands": ops})
    assert r["ok"], r
    s = state(web)
    assert s["axes"][0]["busy"] and s["axes"][1]["busy"]          # both started by the same command
    sim.clock.advance(2.0)
    s = state(web)
    assert (s["axes"][0]["position"], s["axes"][1]["position"]) == (1000, -2000)


def test_sync_start_all_or_nothing(web, sim):
    post(web + "/api/axis", {"axis": 2, "cmd": "START_REL", "operands": {"target": 10 ** 6, "speed": 1000}})
    ops = {"1": {"target": 1000, "speed": 10000}, "2": {"target": 1000, "speed": 10000}}
    _, r = post(web + "/api/sync", {"cmd": "START", "mode": "REL", "operands": ops})
    assert not r["ok"] and "Y축 거부: 운전 중" in r["text"]
    assert not state(web)["axes"][0]["busy"]                       # X did not start alone


def test_sync_stop(web, sim):
    ops = {"1": {"target": 10 ** 6, "speed": 1000}, "2": {"target": 10 ** 6, "speed": 1000}}
    post(web + "/api/sync", {"cmd": "START", "mode": "REL", "operands": ops})
    _, r = post(web + "/api/sync", {"cmd": "STOP"})
    assert r["ok"], r
    sim.clock.advance(2.0)
    s = state(web)
    assert not s["axes"][0]["busy"] and not s["axes"][1]["busy"]


def test_sync_rel_zero_target_refused(web):
    _, r = post(web + "/api/sync", {"cmd": "START", "mode": "REL", "operands": {"1": {"target": 0}, "2": {"target": 5}}})
    assert not r["ok"] and "X축 거부: 값 범위" in r["text"]


LINE = {"mode": 1, "abs": 0, "end_x": 3000, "end_y": 4000, "feed": 10000, "accel_ms": 100, "decel_ms": 100}
ARC_CCW = {"mode": 3, "abs": 0, "end_x": 2000, "end_y": 2000, "center_i": 0, "center_j": 2000, "feed": 5000,
           "accel_ms": 100, "decel_ms": 100}          # quarter circle r = 2000 from (0,0) to (2000,2000), CCW


def test_interp_math_quarter_circle():
    from svtool.interp import Path, ARC_CCW as CCW, ARC_CW as CW
    p = Path(CCW, 2000, 2000, 0, 2000, 5000, 100, 100)
    assert abs(p.len - 3141.59) < 0.1 and abs(p.r - 2000) < 1e-9            # (pi/2) * 2000
    x, y = p.xy(p.len / 2)
    assert abs(x - 2000 * 0.70711) < 0.1 and abs(y - (2000 - 2000 * 0.70711)) < 0.1   # passes below the centre (CCW)
    q = Path(CW, 2000, 2000, 0, 2000, 5000, 100, 100)
    assert abs(q.len - 3 * 3141.59) < 0.5                                   # CW goes the long way (270 degrees)
    full = Path(CCW, 0, 0, 1000, 0, 5000, 100, 100)
    assert abs(full.len - 6283.19) < 0.1                                    # end == start: full circle


def test_interp_preview_and_geometry_error(web):
    _, r = post(web + "/api/interp/preview", LINE)
    assert r["ok"] and r["length"] == 5000.0 and r["end"] == [3000, 4000]
    assert r["axis_peak"] == [6000, 8000] and r["points"][-1][1:] == [3000, 4000]
    _, r = post(web + "/api/interp/preview", dict(ARC_CCW, end_x=2500))
    assert not r["ok"] and "원 위에" in r["text"]


def test_interp_linear_runs_to_exact_end(web, sim):
    _, r = post(web + "/api/interp/start", LINE)
    assert r["ok"], r
    sim.clock.advance(0.3)
    s = state(web)
    assert s["ip"]["state"] == 1
    x, y = s["axes"][0]["position"], s["axes"][1]["position"]
    assert x > 0 and abs(y * 3 - x * 4) <= 8                                 # on the line y = 4/3 x
    sim.clock.advance(2.0)
    s = state(web)
    assert s["ip"]["state"] == 3 and [a["position"] for a in s["axes"]] == [3000, 4000]


def test_interp_arc_passes_the_circle(web, sim):
    _, r = post(web + "/api/interp/start", ARC_CCW)
    assert r["ok"], r
    for _ in range(5):
        sim.clock.advance(0.1)
        s = state(web)
        x, y = s["axes"][0]["position"], s["axes"][1]["position"]
        assert abs(((x - 0) ** 2 + (y - 2000) ** 2) ** 0.5 - 2000) <= 2      # integer rounding only
    sim.clock.advance(2.0)
    assert [a["position"] for a in state(web)["axes"]] == [2000, 2000]


def test_interp_stop_decelerates_on_the_path(web, sim):
    post(web + "/api/interp/start", dict(LINE, end_x=300000, end_y=400000))
    sim.clock.advance(1.0)
    _, r = post(web + "/api/sync", {"cmd": "STOP"})
    assert r["ok"]
    sim.clock.advance(1.0)
    s = state(web)
    x, y = s["axes"][0]["position"], s["axes"][1]["position"]
    assert s["ip"]["state"] == 5 and 0 < x < 300000 and abs(y * 3 - x * 4) <= 8


def test_interp_refused_cases(web, sim):
    _, r = post(web + "/api/interp/start", dict(LINE, feed=2000000))
    assert not r["ok"] and "범위" in r["text"]                                # register range
    _, r = post(web + "/api/interp/start", dict(LINE, feed=1000000))
    assert not r["ok"] and "값 범위" in r["text"]                             # axis max_speed 100000 < 800000
    post(web + "/api/interp/start", LINE)
    _, r = post(web + "/api/interp/start", LINE)
    assert not r["ok"]                                                        # already running


def test_interp_refuses_axis_commands_while_running(web, sim):
    post(web + "/api/interp/start", dict(LINE, end_x=300000, end_y=400000))
    sim.clock.advance(0.2)
    _, r = post(web + "/api/axis", {"axis": 1, "cmd": "SET_POSITION", "operands": {"target": 0}})
    assert not r["ok"] and "운전 중" in r["text"]                              # review C-01
    assert state(web)["ip"]["state"] == 1


def test_interp_arc_end_off_circle_ends_on_programmed_point(web, sim):
    from svtool.interp import Path, ARC_CCW as CCW
    p = Path(CCW, 2001, 2000, 0, 2000, 5000, 100, 100)                       # 1 pulse off the circle: accepted
    assert p.xy(p.len) == pytest.approx((2001, 2000), abs=1e-6)             # path itself ends on the programmed point
    _, r = post(web + "/api/interp/start", dict(ARC_CCW, end_x=2003))       # 3 pulses off: refused
    assert not r["ok"]


def test_sync_and_interp_refuse_latched_stop(web, sim):
    sim.set_stop_input(True)
    sim.set_stop_input(False)                                               # input released, latch kept (D-058)
    _, r = post(web + "/api/interp/start", LINE)
    assert not r["ok"] and "비상정지" in r["text"]                            # review M-01
    _, r = post(web + "/api/sync", {"cmd": "START", "mode": "REL", "operands": {"1": {"target": 10}, "2": {"target": 10}}})
    assert not r["ok"] and "비상정지" in r["text"]


def test_interp_aborts_on_stop_input(web, sim):
    post(web + "/api/interp/start", dict(LINE, end_x=300000, end_y=400000))
    sim.clock.advance(0.5)
    sim.set_stop_input(True)
    s = state(web)
    assert s["ip"]["state"] == 4 and s["ip"]["result"] == 0x20


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
