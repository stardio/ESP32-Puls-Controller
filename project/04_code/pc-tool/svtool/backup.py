"""Backup / restore / reset / status / alarm logic (FR-035)."""
import datetime
import json
from . import regmap as R, alarms
from .device import Device
from .modbus import ModbusException

FORMAT = "svtool-backup"
FORMAT_VERSION = 1


class RestoreError(Exception):
    pass


def make_backup(dev: Device, target="") -> dict:
    ident = dev.identity()
    entries = []
    skipped = []
    for run in R.runs(R.backup_fields(ident["axis_count"])):
        try:
            words = dev.read_words(run[0].addr, sum(f.words for f in run))
        except ModbusException as e:
            if e.code == 2 and run[0].optional:
                skipped.append([f.name for f in run])
                continue
            raise
        pos = 0
        for f in run:
            w = words[pos:pos + f.words]
            pos += f.words
            entries.append({"addr": f.addr, "name": f.name, "group": f.group, "kind": f.kind, "words": w,
                            "value": R.decode(f.kind, w)})
    return {"format": FORMAT, "format_version": FORMAT_VERSION,
            "created": datetime.datetime.now().isoformat(timespec="seconds"), "target": target,
            "model_id": ident["model_id"], "axis_count": ident["axis_count"],
            "fw_version": ident["fw_version"], "fw_version_raw": ident["fw_version_raw"],
            "regmap_version": ident["regmap_version"], "skipped_optional": skipped, "registers": entries}


def save_json(data, path):
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=1)


def load_json(path):
    with open(path, encoding="utf-8") as f:
        d = json.load(f)
    if d.get("format") != FORMAT:
        raise RestoreError("svtool 백업 파일이 아닙니다.")
    return d


def check_versions(dev: Device, data, force=False):
    """Returns (warnings, ident). Raises RestoreError when restore must not proceed."""
    ident = dev.identity()
    warns = []
    if data["regmap_version"] != ident["regmap_version"]:
        msg = f"regmap_version 불일치: 파일 {data['regmap_version']} / 장치 {ident['regmap_version']}"
        if not force:
            raise RestoreError(msg + " (--force 로 무시 가능, 권장하지 않음)")
        warns.append(msg)
    if data["axis_count"] > ident["axis_count"]:
        raise RestoreError(f"파일은 {data['axis_count']}축 모델, 장치는 {ident['axis_count']}축 모델입니다.")
    if data["fw_version"] != ident["fw_version"]:
        warns.append(f"펌웨어 버전 다름: 파일 {data['fw_version']} / 장치 {ident['fw_version']}")
    return warns, ident


def compute_diff(dev: Device, data, include_net=True):
    """List of (entry, current_words) for entries whose device value differs from the file."""
    fields = {f.addr: f for f in R.backup_fields(2)}
    diffs = []
    for e in data["registers"]:
        if e["group"] == "network" and not include_net:
            continue
        f = fields.get(e["addr"])
        if f is None:
            continue
        try:
            cur = dev.read_words(e["addr"], len(e["words"]))
        except ModbusException as ex:
            if ex.code == 2:
                diffs.append((e, None))
                continue
            raise
        if cur != e["words"]:
            diffs.append((e, cur))
    return diffs


def format_diff(diffs):
    if not diffs:
        return ["차이 없음: 장치 값이 파일과 같습니다."]
    lines = [f"{'항목':<26}{'주소':<8}{'장치 현재값':>16}{'파일 값':>16}"]
    for e, cur in diffs:
        cv = "(읽기 불가)" if cur is None else str(R.decode(e["kind"], cur))
        lines.append(f"{e['name']:<26}0x{e['addr']:04X}  {cv:>16}{e['value']:>16}")
    return lines


def apply_restore(dev: Device, data, diffs, include_net=True, log=print):
    """Unlock, write changed entries, SAVE, verify. Returns list of error strings."""
    errors = []
    fields = {f.addr: f for f in R.backup_fields(2)}
    last_group = None
    dev.unlock()
    for e, _cur in diffs:
        if e["group"] != last_group:
            dev.unlock()             # window is 10 s: refresh per group
            last_group = e["group"]
        try:
            dev.c.write_registers(e["addr"], e["words"])
        except ModbusException as ex:
            errors.append(f"쓰기 거부 {e['name']} (0x{e['addr']:04X}): 예외 {ex.code:02d}")
    res = dev.sys_command(R.CMD_SAVE)
    if res != R.RESULT_OK:
        errors.append(f"SAVE 실패: sys_cmd_result=0x{res:02X}")
    for e, _cur in diffs:
        try:
            now = dev.read_words(e["addr"], len(e["words"]))
        except ModbusException:
            continue
        if now != e["words"] and not any(e["name"] in x for x in errors):
            errors.append(f"확인 불일치 {e['name']}: 쓴 값 {e['words']} / 읽은 값 {now}")
    return errors


def do_reset(dev: Device, which: str, reboot=False):
    cmd = R.CMD_FACTORY_RESET if which == "factory" else R.CMD_NET_RESET
    dev.unlock()
    res = dev.sys_command(cmd)
    if res == R.RESULT_OK and reboot:
        dev.unlock()
        dev.c.write_register(R.SYS_COMMAND, R.CMD_REBOOT)
    return res


RESULT_TEXT = {0: "성공", 1: "실행 중", 0x10: "거부: 조건 불충족", 0x11: "거부: 잠금", 0x12: "거부: 축 운전 중"}


def wiring_report(word: int, axis_count: int, comm_mode_active=0):
    """Return list of (label, ok, hint) and overall verdict."""
    items = []
    if not word & (1 << R.W_VALID):
        return [("점검 유효", False, "점검이 아직 유효하지 않습니다(부팅 직후이거나 점검 미수행). 잠시 후 다시 확인하세요.")], False
    names = {1: "X", 2: "Y"}
    for n in range(1, axis_count + 1):
        b = (n - 1) * 4
        nm = names[n]
        items += [
            (f"{nm}축 STOP 입력", bool(word >> (b + R.W_STOP_OK) & 1), "STOP이 눌려 있거나 NC 배선이 열려 있습니다. 배선을 확인하세요."),
            (f"{nm}축 START 입력", bool(word >> (b + R.W_START_WAIT) & 1), "START 입력이 이미 활성입니다. 배선·극성을 확인하세요."),
            (f"{nm}축 펄스 루프백", bool(word >> (b + R.W_LOOPBACK) & 1), "펄스 출력이 되읽히지 않습니다. 펄스 출력 배선을 확인하세요."),
        ]
    items.append(("이더넷 링크", bool(word >> R.W_ETH_LINK & 1), "LAN 케이블/스위치를 확인하세요."))
    if comm_mode_active == 1:
        items.append(("RTU 수신", bool(word >> R.W_RTU_RX & 1), "RS-485 배선(A/B), 통신 속도·패리티를 확인하세요."))
    return items, all(ok for _, ok, _ in items)


def read_status(dev: Device):
    ident = dev.identity()
    w = dev.read_words(R.WIRING_STATUS, 1)[0]
    mode = dev.read_words(R.COMM_MODE_ACTIVE, 1)[0]
    items, ok = wiring_report(w, ident["axis_count"], mode)
    svon = {n: bool(w >> ((n - 1) * 4 + R.W_SVON_OUT) & 1) for n in range(1, ident["axis_count"] + 1)}
    return ident, w, items, ok, svon
