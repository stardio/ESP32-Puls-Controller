"""python -m svtool ..."""
import argparse
import sys

from . import regmap as R, alarms, backup as B
from .device import Device, open_target
from .modbus import ModbusError, ModbusException

PHRASES = {"factory": "FACTORY RESET", "net": "NET RESET"}


def _connect(a):
    parity = {"E": "E", "N": "N", "O": "O"}[a.parity.upper()]
    return Device(open_target(a.target, unit=a.unit, baud=a.baud, parity=parity, timeout=a.timeout))


def _confirm(a, phrase, what):
    if a.confirm is not None:
        ok = a.confirm == phrase
    else:
        print(f"{what}\n계속하려면 정확히 '{phrase}' 를 입력하세요: ", end="", flush=True)
        ok = sys.stdin.readline().strip() == phrase
    if not ok:
        print("확인 문구가 일치하지 않아 취소했습니다.")
    return ok


def cmd_backup(a):
    dev = _connect(a)
    data = B.make_backup(dev, a.target)
    B.save_json(data, a.output)
    print(f"백업 완료: {a.output} (레지스터 항목 {len(data['registers'])}개, 펌웨어 {data['fw_version']}, regmap_version {data['regmap_version']})")
    if data["skipped_optional"]:
        print("이 모델에 없는 선택 항목은 건너뜀:", data["skipped_optional"])
    return 0


def cmd_restore(a):
    data = B.load_json(a.file)
    dev = _connect(a)
    warns, _ = B.check_versions(dev, data, a.force)
    for w in warns:
        print("경고:", w)
    diffs = B.compute_diff(dev, data, not a.no_net)
    print("\n".join(B.format_diff(diffs)))
    if not diffs or a.dry_run:
        return 0
    if not a.yes:
        print("위 항목을 장치에 쓰고 저장합니까? [y/N]: ", end="", flush=True)
        if sys.stdin.readline().strip().lower() != "y":
            print("취소했습니다.")
            return 1
    errs = B.apply_restore(dev, data, diffs, not a.no_net)
    if errs:
        print("복원 중 문제:")
        for e in errs:
            print(" -", e)
        return 2
    print(f"복원 완료: {len(diffs)}개 항목 쓰기, SAVE 완료. 네트워크 항목은 재부팅 후 적용됩니다.")
    return 0


def _reset(a, which):
    what = "공장 초기화: 모든 설정이 출하값으로 돌아갑니다." if which == "factory" else "네트워크 초기화: IP 등 네트워크 설정이 출하값으로 돌아갑니다."
    if not _confirm(a, PHRASES[which], what):
        return 1
    dev = _connect(a)
    res = B.do_reset(dev, which, a.reboot)
    print("결과:", B.RESULT_TEXT.get(res, f"0x{res:02X}"))
    if res == R.RESULT_OK:
        print("완료. 적용하려면 재부팅이 필요합니다." if not a.reboot else "재부팅을 요청했습니다.")
    return 0 if res == R.RESULT_OK else 2


def cmd_status(a):
    dev = _connect(a)
    ident, w, items, ok, svon = B.read_status(dev)
    print(f"펌웨어 {ident['fw_version']}  regmap {ident['regmap_version']}  {ident['axis_count']}축  wiring_status=0x{w:04X}")
    for label, good, hint in items:
        print(f"  [{'정상' if good else '이상'}] {label}" + ("" if good else f"  -> {hint}"))
    for n, on in svon.items():
        print(f"  (참고) {'XY'[n-1]}축 서보 ON 출력: {'ON' if on else 'OFF'}")
    for n in range(1, ident["axis_count"] + 1):
        nm = "XY"[n - 1]
        bad = [l for l, g, _ in items if l.startswith(nm + "축") and not g]
        print(f"{nm}축 종합: {'정상' if not bad else '이상 (' + ', '.join(bad) + ')'}")
    print("전체:", "정상" if ok else "이상")
    return 0 if ok else 3


def cmd_alarm(a):
    dev = _connect(a)
    ident = dev.identity()
    table = alarms.load(a.messages)
    hint = dev.read_words(R.ALARM_HINT, 1)[0]
    rows = [("시스템 최우선", hint)]
    for n in range(1, ident["axis_count"] + 1):
        rows.append((f"{'XY'[n-1]}축", dev.read_words(R.AXIS_BASE[n] + R.A_ALARM, 1)[0]))
    any_alarm = False
    for label, code in rows:
        if code == 0:
            print(f"{label}: 알람 없음")
            continue
        any_alarm = True
        name, cause, action = alarms.describe(code, table)
        print(f"{label}: 0x{code:04X} {name}\n  원인: {cause}\n  조치: {action}")
    if a.history:
        n = dev.read_words(R.HIST_COUNT, 1)[0]
        for i in range(min(n, R.HIST_N)):
            w = dev.read_words(R.HIST_BASE + 4 * i, 4)
            print(f"  이력[{i}] 0x{w[0]:04X} 축={w[1]} 가동 {w[2] | (w[3] << 16)} s  {alarms.describe(w[0], table)[0]}")
    return 4 if any_alarm else 0


def cmd_sim(a):
    from .sim import run_server
    return run_server(a.port, a.axes, a.host, not a.no_console)


def build_parser():
    p = argparse.ArgumentParser(prog="svtool", description="ESP32 펄스 서보 컨트롤러 PC 도구")
    sub = p.add_subparsers(dest="cmd", required=True)

    def common(sp):
        sp.add_argument("--target", required=True, help="IP[:포트] 또는 COMx(RTU, pyserial 필요)")
        sp.add_argument("--unit", type=int, default=1, help="Modbus 유닛 ID (기본 1)")
        sp.add_argument("--baud", type=int, default=19200, help="RTU 속도 (기본 19200 [권고])")
        sp.add_argument("--parity", default="E", help="RTU 패리티 E/N/O (기본 E [권고])")
        sp.add_argument("--timeout", type=float, default=2.0)

    s = sub.add_parser("backup"); common(s); s.add_argument("-o", "--output", required=True); s.set_defaults(fn=cmd_backup)
    s = sub.add_parser("restore"); common(s); s.add_argument("file")
    s.add_argument("--yes", action="store_true", help="확인 없이 쓰기"); s.add_argument("--dry-run", action="store_true")
    s.add_argument("--force", action="store_true", help="regmap_version 불일치 무시")
    s.add_argument("--no-net", action="store_true", help="네트워크 설정은 복원하지 않음"); s.set_defaults(fn=cmd_restore)
    for name, w in (("factory-reset", "factory"), ("net-reset", "net")):
        s = sub.add_parser(name); common(s)
        s.add_argument("--confirm", default=None, help=f"확인 문구를 인수로 전달 ('{PHRASES[w]}')")
        s.add_argument("--reboot", action="store_true"); s.set_defaults(fn=lambda a, w=w: _reset(a, w))
    s = sub.add_parser("status"); common(s); s.set_defaults(fn=cmd_status)
    s = sub.add_parser("alarm"); common(s); s.add_argument("--messages", help="alarm-messages.md 경로")
    s.add_argument("--history", action="store_true"); s.set_defaults(fn=cmd_alarm)
    s = sub.add_parser("sim", help="Modbus TCP 시뮬레이터")
    s.add_argument("--port", type=int, default=5020); s.add_argument("--axes", type=int, default=2, choices=(1, 2))
    s.add_argument("--host", default="0.0.0.0"); s.add_argument("--no-console", action="store_true")
    s.set_defaults(fn=cmd_sim)
    return p


def main(argv=None):
    a = build_parser().parse_args(argv)
    try:
        return a.fn(a)
    except B.RestoreError as e:
        print("오류:", e)
        return 2
    except ModbusException as e:
        print(f"장치가 요청을 거부했습니다: {e}")
        return 2
    except (ModbusError, OSError) as e:
        print(f"통신 오류: {e}")
        return 2
