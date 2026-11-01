import json

import pytest

from svtool import regmap as R, units, alarms, backup as B
from svtool.cli import main
from svtool.modbus import ModbusException

X, Y = R.AXIS_BASE[1], R.AXIS_BASE[2]


def w32(dev, addr, v, kind="u32"):
    dev.c.write_registers(addr, R.encode(kind, v))


def r32(dev, addr, kind="u32"):
    return R.decode(kind, dev.c.read_holding(addr, 2))


# ---------- backup / restore ----------
def test_backup_file_has_versions_and_all_groups(target, tmp_path):
    out = tmp_path / "b.json"
    assert main(["backup", "--target", target, "-o", str(out)]) == 0
    d = json.loads(out.read_text(encoding="utf-8"))
    assert d["regmap_version"] == 2 and d["fw_version"] == "0.1.0" and d["axis_count"] == 2
    groups = {e["group"] for e in d["registers"]}
    assert groups == {"system", "axis1", "axis2", "points", "network", "legacy"}
    names = {e["name"] for e in d["registers"]}
    assert {"X.max_speed", "Y.unit_mode", "X.pt0.pos_unit", "ip_addr"} <= names


def test_roundtrip_backup_change_restore(target, dev, tmp_path, capsys, sim):
    out = tmp_path / "b.json"
    assert main(["backup", "--target", target, "-o", str(out)]) == 0
    orig = json.loads(out.read_text(encoding="utf-8"))["registers"]
    # change parameters, points, system and network values
    dev.unlock()
    w32(dev, X + 0x104, 55555)
    dev.c.write_register(X + 0x140, 1)
    dev.c.write_register(Y + 0x147, 120)
    w32(dev, R.POINT_BASE + 0, 123456, "s32")
    dev.c.write_register(0x0111, 2500)
    w32(dev, 0x4000, 0xC0A80164)
    assert main(["backup", "--target", target, "-o", str(tmp_path / "changed.json")]) == 0
    assert json.loads((tmp_path / "changed.json").read_text(encoding="utf-8"))["registers"] != orig
    capsys.readouterr()
    assert main(["restore", str(out), "--target", target, "--yes"]) == 0
    text = capsys.readouterr().out
    assert "X.max_speed" in text and "55555" in text and "100000" in text      # diff shown before writing
    assert main(["backup", "--target", target, "-o", str(tmp_path / "after.json")]) == 0
    after = json.loads((tmp_path / "after.json").read_text(encoding="utf-8"))["registers"]
    assert [(e["addr"], e["words"]) for e in after] == [(e["addr"], e["words"]) for e in orig]
    assert not sim.unsaved                                              # SAVE done


def test_restore_dry_run_and_no_changes(target, dev, tmp_path, capsys):
    out = tmp_path / "b.json"
    main(["backup", "--target", target, "-o", str(out)])
    capsys.readouterr()
    assert main(["restore", str(out), "--target", target, "--dry-run"]) == 0
    assert "차이 없음" in capsys.readouterr().out


def test_restore_refuses_regmap_mismatch(target, tmp_path, capsys):
    out = tmp_path / "b.json"
    main(["backup", "--target", target, "-o", str(out)])
    d = json.loads(out.read_text(encoding="utf-8"))
    d["regmap_version"] = 3
    out.write_text(json.dumps(d), encoding="utf-8")
    assert main(["restore", str(out), "--target", target, "--yes"]) == 2
    assert "regmap_version 불일치" in capsys.readouterr().out


def test_restore_warns_on_fw_difference(target, tmp_path, capsys):
    out = tmp_path / "b.json"
    main(["backup", "--target", target, "-o", str(out)])
    d = json.loads(out.read_text(encoding="utf-8"))
    d["fw_version"] = "9.9.9"
    out.write_text(json.dumps(d), encoding="utf-8")
    main(["restore", str(out), "--target", target, "--dry-run"])
    assert "펌웨어 버전 다름" in capsys.readouterr().out


# ---------- lock ----------
def test_network_write_without_unlock_rejected(dev):
    with pytest.raises(ModbusException) as e:
        w32(dev, 0x4000, 0x0A000001)
    assert e.value.code == 3
    assert r32(dev, 0x4000) == 0xC0A800DC
    w32(dev, 0x4000, 0xC0A800DC)                   # same value is allowed


def test_unlock_expires_after_10s(dev, sim):
    dev.unlock()
    sim.advance(9.5)
    w32(dev, 0x4000, 0x0A000001)
    sim.advance(1.0)
    with pytest.raises(ModbusException):
        w32(dev, 0x4000, 0x0A000002)


def test_wrong_unlock_key_rejected(dev):
    with pytest.raises(ModbusException) as e:
        dev.c.write_register(R.UNLOCK_KEY_ADDR, 0x1234)
    assert e.value.code == 3


def test_locked_commands_refused_without_unlock(dev):
    dev.c.write_register(R.SYS_COMMAND, R.CMD_FACTORY_RESET)
    assert dev.c.read_holding(R.SYS_CMD_RESULT, 1)[0] == R.RESULT_LOCKED


def test_half_32bit_write_and_readonly_and_unmapped(dev):
    with pytest.raises(ModbusException) as e:
        dev.c.write_register(X + 0x104, 5)
    assert e.value.code == 3
    with pytest.raises(ModbusException) as e:
        dev.c.write_register(R.FW_VERSION, 1)
    assert e.value.code == 2
    with pytest.raises(ModbusException) as e:
        dev.c.read_holding(0x7000, 1)
    assert e.value.code == 2


def test_param_range_exception_03(dev):
    with pytest.raises(ModbusException) as e:
        dev.c.write_register(X + 0x145, 0)           # gear_num 1..1000
    assert e.value.code == 3


# ---------- resets ----------
def test_factory_reset_needs_phrase_and_works(target, dev, sim, capsys):
    dev.c.write_register(X + 0x140, 2)
    assert main(["factory-reset", "--target", target, "--confirm", "wrong"]) == 1
    assert dev.c.read_holding(X + 0x140, 1)[0] == 2
    assert main(["factory-reset", "--target", target, "--confirm", "FACTORY RESET"]) == 0
    assert dev.c.read_holding(X + 0x140, 1)[0] == 0


def test_net_reset_only_network(target, dev):
    dev.unlock()
    w32(dev, 0x4000, 0x0A000001)
    dev.c.write_register(X + 0x140, 2)
    assert main(["net-reset", "--target", target, "--confirm", "NET RESET"]) == 0
    assert r32(dev, 0x4000) == 0xC0A800DC
    assert dev.c.read_holding(X + 0x140, 1)[0] == 2


# ---------- status / alarm ----------
def test_status_normal_then_fault(target, sim, capsys):
    assert main(["status", "--target", target]) == 0
    out = capsys.readouterr().out
    assert "전체: 정상" in out and "X축 종합: 정상" in out
    sim.console("stop on")
    sim.console("wiring y.loop off")
    assert main(["status", "--target", target]) == 3
    out = capsys.readouterr().out
    assert "X축 종합: 이상" in out and "Y축 종합: 이상" in out and "[이상] Y축 펄스 루프백" in out


def test_wiring_report_decode():
    items, ok = B.wiring_report(0b1000_0001_1111_1111 | (1 << 15), 2)
    assert ok
    items, ok = B.wiring_report(0xFFFF & ~(1 << 1), 2)
    assert not ok and [l for l, g, _ in items if not g] == ["X축 START 입력"]
    items, ok = B.wiring_report(0x00FF, 2)
    assert not ok and items[0][0] == "점검 유효"


def test_alarm_output_korean(target, sim, capsys):
    assert main(["alarm", "--target", target]) == 0
    assert "알람 없음" in capsys.readouterr().out
    sim.console("alarm 1 0x0301")
    assert main(["alarm", "--target", target, "--history"]) == 4
    out = capsys.readouterr().out
    assert "0x0301" in out and "원인:" in out and "조치:" in out and "이력[0]" in out


def test_alarm_table_parse_markdown():
    t = alarms.parse_markdown("| 코드 | 이름 | 원인 | 조치 |\n|---|---|---|---|\n| 0x0301 | 드라이브 | 원인A | 조치B |\n")
    assert t[0x0301] == ("드라이브", "원인A", "조치B")
    assert alarms.describe(0x9999)[0].startswith("미등록")


# ---------- units (FR-029) ----------
def test_unit_conversion_mm():
    # 10000 pulse/rev, 10 mm/rev, gear 1:1 -> 1000 pulse/mm
    assert units.to_pulses(25_000, 1, 10000, 10000, 1, 1) == 25_000   # 25 mm = 25000 um -> 25000 pulses
    assert units.to_units(1000, 1, 10000, 10000, 1, 1) == 1000
    # gear 2 motor : 1 load revs
    assert units.to_pulses(10_000, 1, 10000, 10000, 2, 1) == 20_000
    assert units.to_pulses(90_000, 2, 10000, 0 + 1, 1, 1) == 2500        # 90 deg of 10000/rev
    assert units.rpm_to_pps(60, 10000) == 10000
    assert units.max_rpm(100000, 10000) == 600
    assert units.max_rpm(2_000_000, 10000) == 6000


def test_sim_unit_move_and_position_unit(dev, sim):
    dev.c.write_register(X + 0x140, 1)               # mm
    w32(dev, X + 0x141, 10000)
    w32(dev, X + 0x143, 5000)                        # 5 mm/rev -> 2000 pulse/mm
    w32(dev, X + R.A_TARGET_UNIT, 10_000, "s32")     # 10 mm
    w32(dev, X + R.A_SPEED_UNIT, 20_000)             # 20 mm/s = 40000 pps
    dev.c.write_register(X + R.A_CMD, R.AC["START_ABS_UNIT"])
    assert dev.c.read_holding(X + R.A_CMDRES, 1)[0] == 0
    assert r32(dev, X + R.A_TARGET_ACT, "s32") == 20000
    sim.advance(0.25)
    mid = r32(dev, X + R.A_CMDPOS, "s32")
    assert 9000 < mid < 11000 and dev.c.read_holding(X + R.A_STATE, 1)[0] == 2
    sim.advance(0.5)
    assert dev.c.read_holding(X + R.A_STATE, 1)[0] == 1
    assert dev.c.read_holding(X + R.A_STATUS, 1)[0] & R.ST_DONE
    assert r32(dev, X + R.A_POSUNIT, "s32") == 10_000
    assert r32(dev, X + R.A_PULSES_DONE) == 20000


def test_unit_overspeed_rejected_0x17(dev):
    dev.c.write_register(X + 0x140, 1)
    w32(dev, X + R.A_TARGET_UNIT, 1000, "s32")
    w32(dev, X + R.A_SPEED_UNIT, 100_000)            # 100 mm/s x 1000 pulse/mm = 100 kpps = max_speed
    dev.c.write_register(X + R.A_CMD, R.AC["START_ABS_UNIT"])
    assert dev.c.read_holding(X + R.A_CMDRES, 1)[0] == 0      # 100 kpps = max_speed exactly
    # gear 2:1 doubles the pulse rate -> 200 kpps exceeds max_speed
    dev.c.write_register(X + R.A_CMD, R.AC["QUICK_STOP"])
    dev.c.write_register(X + 0x145, 2)
    dev.c.write_register(X + R.A_CMD, R.AC["START_ABS_UNIT"])
    assert dev.c.read_holding(X + R.A_CMDRES, 1)[0] == R.CR_RANGE


def test_max_rpm_calc_register(dev):
    assert dev.c.read_holding(X + R.A_MAXRPM, 1)[0] == 600


def test_test_run_hold_500ms(dev, sim):
    dev.c.write_register(X + R.A_CMD, R.AC["TEST_POS"])
    assert dev.c.read_holding(X + R.A_STATE, 1)[0] == 3
    sim.advance(0.4)
    dev.c.write_register(X + R.A_CMD, R.AC["TEST_POS"])       # hold
    sim.advance(0.4)
    assert dev.c.read_holding(X + R.A_STATE, 1)[0] == 3
    sim.advance(0.3)
    sim.advance(0.5)
    assert dev.c.read_holding(X + R.A_STATE, 1)[0] == 1
    assert r32(dev, X + R.A_CMDPOS, "s32") > 0


# ---------- points (FR-034) ----------
def test_point_teach_and_go(dev, sim):
    dev.c.write_register(X + 0x140, 1)
    w32(dev, X + 0x143, 10000)                       # 1000 pulse/mm
    w32(dev, X + R.A_TARGET, 5000, "s32")
    dev.c.write_register(X + R.A_CMD, R.AC["SET_POSITION"])
    w32(dev, X + R.A_SPEED_UNIT, 50_000)
    dev.c.write_register(X + R.A_POINT_INDEX, 3)
    dev.c.write_register(X + R.A_CMD, R.AC["POINT_TEACH"])
    base = R.POINT_BASE + 3 * 0x10
    assert r32(dev, base, "s32") == 5000 and dev.c.read_holding(base + 6, 1)[0] == 1
    w32(dev, X + R.A_TARGET, 0, "s32")
    dev.c.write_register(X + R.A_CMD, R.AC["SET_POSITION"])
    dev.c.write_register(X + R.A_CMD, R.AC["POINT_GO"])
    sim.advance(5)
    assert r32(dev, X + R.A_CMDPOS, "s32") == 5000
    dev.c.write_register(X + R.A_POINT_INDEX, 4)               # not taught
    dev.c.write_register(X + R.A_CMD, R.AC["POINT_GO"])
    assert dev.c.read_holding(X + R.A_CMDRES, 1)[0] == R.CR_RANGE


# ---------- motion / stop / alarm simulation ----------
def _move(dev, target, speed=10000, rel=False):
    w32(dev, X + R.A_TARGET, target, "s32")
    w32(dev, X + R.A_SPEED, speed)
    dev.c.write_register(X + R.A_CMD, R.AC["START_REL" if rel else "START_ABS"])


def test_move_progress_and_done(dev, sim):
    _move(dev, 5000)
    sim.advance(0.2)
    assert 1500 < r32(dev, X + R.A_CMDPOS, "s32") < 2500
    sim.advance(1)
    assert r32(dev, X + R.A_CMDPOS, "s32") == 5000
    assert dev.c.read_holding(X + R.A_STATUS, 1)[0] & R.ST_DONE


def test_stop_command_decelerates(dev, sim):
    _move(dev, 1_000_000)
    sim.advance(0.3)
    dev.c.write_register(X + R.A_CMD, R.AC["STOP"])
    assert dev.c.read_holding(X + R.A_STATE, 1)[0] == 5
    sim.advance(0.5)
    assert dev.c.read_holding(X + R.A_STATE, 1)[0] == 1
    assert dev.c.read_holding(X + R.A_STOPCAUSE, 1)[0] == 2


def test_stop_input_latch_and_release_by_start(dev, sim):
    _move(dev, 1_000_000)
    sim.advance(0.1)
    sim.console("stop on")
    assert dev.c.read_holding(R.ALARM_HINT, 1)[0] == 0x0101
    assert dev.c.read_holding(X + R.A_STATE, 1)[0] == 7
    _move(dev, 100)
    assert dev.c.read_holding(X + R.A_CMDRES, 1)[0] == R.CR_ESTOP
    sim.console("stop off")
    assert dev.c.read_holding(R.ESTOP_STATE, 1)[0] == 2
    _move(dev, 100, rel=True)                                  # D-072: start releases the latch
    assert dev.c.read_holding(X + R.A_CMDRES, 1)[0] == 0
    assert dev.c.read_holding(R.ESTOP_STATE, 1)[0] == 0


def test_alarm_blocks_start_until_reset(dev, sim):
    sim.console("alarm 1 0x0301")
    assert dev.c.read_holding(R.ALARM_AXIS_MASK, 1)[0] == 1
    _move(dev, 100)
    assert dev.c.read_holding(X + R.A_CMDRES, 1)[0] == R.CR_ALARM
    dev.c.write_register(X + R.A_CMD, R.AC["ALARM_RESET"])
    _move(dev, 100)
    assert dev.c.read_holding(X + R.A_CMDRES, 1)[0] == 0


def test_soft_limit_rejects_0x14(dev):
    dev.c.write_register(X + 0x11A, 1)
    w32(dev, X + 0x11B, 1000, "s32")
    _move(dev, 5000)
    assert dev.c.read_holding(X + R.A_CMDRES, 1)[0] == R.CR_SWLIM


def test_legacy_window_coil_start(dev, sim):
    w32(dev, 0x0000, 3000)
    w32(dev, 0x0002, 10000)
    dev.c.write_coil(3, True)
    dev.c.write_coil(0, True)
    sim.advance(1)
    assert r32(dev, X + R.A_CMDPOS, "s32") == 3000
    assert dev.c.read_holding(0x000D, 1)[0] == 1
    assert r32(dev, 0x0006) == 3000
    dev.c.write_coil(3, False)
    dev.c.write_coil(0, True)
    sim.advance(1)
    assert r32(dev, X + R.A_CMDPOS, "s32") == 0


def test_reboot_loses_unsaved(dev, sim):
    dev.c.write_register(X + 0x140, 2)
    dev.unlock()
    dev.c.write_register(R.SYS_COMMAND, R.CMD_REBOOT)
    assert dev.c.read_holding(X + 0x140, 1)[0] == 0


def test_one_axis_sim_rejects_axis2():
    from svtool.sim import Sim
    s = Sim(1)
    with pytest.raises(Exception):
        s.read_regs(Y, 1)
