"""Register map in ONE place (interface-spec section 5, regmap_version 2, 5.12 additions).

If the firmware coder moves an offset, change it only here. Items marked [REC] are recommended defaults
chosen by the tool author because the spec leaves them open (rule 9 of CLAUDE.md).
"""
from dataclasses import dataclass
from typing import List, Optional

REGMAP_VERSION = 2
UNLOCK_KEY = 0x4D56            # firmware.md section 12
UNLOCK_WINDOW_S = 10.0

# ---- system block (0x0020..) -------------------------------------------------
MODEL_ID, AXIS_COUNT, FW_VERSION, REGMAP_VER = 0x0020, 0x0021, 0x0022, 0x0024
SYS_STATUS, ESTOP_STATE, TCP_CLIENTS, UPTIME_S, HEARTBEAT = 0x0025, 0x0026, 0x0027, 0x0028, 0x002A
SYS_INPUTS, RESET_REASON, BOOT_STATUS, COMM_MODE_ACTIVE = 0x002B, 0x002C, 0x002D, 0x002E
ALARM_AXIS_MASK, DI_RAW, DO_STATE, WIRING_STATUS, ALARM_HINT = 0x0030, 0x0031, 0x0032, 0x0033, 0x0034
SYS_COMMAND, SYS_CMD_RESULT, UNLOCK_KEY_ADDR = 0x0100, 0x0101, 0x0102
USER_DO, NET_APPLY = 0x0120, 0x400F
HIST_COUNT, HIST_BASE = 0x3000, 0x3010

# sys_command values. 1..6 per spec 5.3; 7/8 FACTORY_RESET/NET_RESET: numbers fixed by spec 5.12 (implementation-confirmed table).
CMD_ESTOP_RELEASE, CMD_SAVE, CMD_LOAD_DEFAULT, CMD_REBOOT, CMD_CLEAR_HIST, CMD_STOP_ALL = 1, 2, 3, 4, 5, 6
CMD_FACTORY_RESET, CMD_NET_RESET = 7, 8
RESULT_OK, RESULT_BUSY, RESULT_REFUSED, RESULT_LOCKED, RESULT_RUNNING = 0, 1, 0x10, 0x11, 0x12

# axis_command values (5.5 + 5.12)
AC = dict(START_ABS=1, START_REL=2, STOP=3, HOME=4, ALARM_RESET=5, SERVO_ON=6, SERVO_OFF=7, JOG_POS=8, JOG_NEG=9,
          JOG_STOP=10, SET_POSITION=11, QUICK_STOP=12, START_ABS_UNIT=13, START_REL_UNIT=14, TEST_POS=15,
          TEST_NEG=16, POINT_GO=17, POINT_TEACH=18)
# axis cmd_result
CR_OK, CR_ESTOP, CR_ALARM, CR_SVOFF, CR_RUNNING, CR_SWLIM, CR_LIMDIR, CR_POSINV, CR_RANGE = 0, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17

# axis_status bits
ST_READY, ST_BUSY, ST_DONE, ST_HOMED, ST_ALARM, ST_ESTOP = 1 << 0, 1 << 1, 1 << 2, 1 << 3, 1 << 4, 1 << 5
ST_SVON, ST_POSINV, ST_SWP, ST_SWN = 1 << 10, 1 << 11, 1 << 12, 1 << 13

# wiring_status bits (5.12). Per axis: X at bit 0..3, Y at bit 4..7.
W_STOP_OK, W_START_WAIT, W_LOOPBACK, W_SVON_OUT = 0, 1, 2, 3
W_ETH_LINK, W_RTU_RX, W_VALID = 8, 9, 15

AXIS_BASE = {1: 0x1000, 2: 0x2000}
POINT_BASE = 0x6000
POINT_COUNT = 8
LEGACY_COIL_AXIS = {1: 0x10, 2: 0x20}   # [REC] spec says 0x10*n; n is 1-based (n=0 would collide with legacy coils 0..3)

# axis offsets
A_STATE, A_STATUS, A_CMDPOS, A_CMDSPEED, A_TARGET_ACT, A_ALARM, A_STOPCAUSE = 0x000, 0x001, 0x002, 0x004, 0x006, 0x008, 0x009
A_INPUTS, A_CMDRES, A_MOVECOUNT, A_PULSES_DONE, A_POSUNIT, A_MAXRPM = 0x00A, 0x00B, 0x00C, 0x00E, 0x030, 0x032
A_CMD, A_TARGET, A_SPEED, A_ACCEL, A_DECEL, A_JOGOVR = 0x040, 0x042, 0x044, 0x046, 0x047, 0x048
A_TARGET_UNIT, A_SPEED_UNIT, A_POINT_INDEX = 0x04A, 0x04C, 0x04E
A_ALARM2, A_ALARM_COUNT = 0x200, 0x201


@dataclass(frozen=True)
class Field:
    name: str
    addr: int
    kind: str                    # u16 s16 u32 s32 bits
    rw: bool = True
    default: int = 0
    lo: Optional[int] = None
    hi: Optional[int] = None
    persist: bool = False
    group: str = ""              # backup group; "" = not backed up
    optional: bool = False       # absent on some models (exception 02 is tolerated)
    net: bool = False            # needs unlock to change

    @property
    def words(self) -> int:
        return 2 if self.kind in ("u32", "s32") else 1


def encode(kind: str, value: int) -> List[int]:
    if kind == "u32":
        value &= 0xFFFFFFFF
        return [value & 0xFFFF, value >> 16]
    if kind == "s32":
        value &= 0xFFFFFFFF
        return [value & 0xFFFF, value >> 16]
    return [value & 0xFFFF]


def decode(kind: str, words: List[int]) -> int:
    """32-bit values: LOW word first (spec 5.1)."""
    if kind in ("u32", "s32"):
        v = words[0] | (words[1] << 16)
        if kind == "s32" and v & 0x80000000:
            v -= 1 << 32
        return v
    v = words[0]
    if kind == "s16" and v & 0x8000:
        v -= 1 << 16
    return v


def _f(name, addr, kind, rw=True, default=0, lo=None, hi=None, persist=False, group="", optional=False, net=False):
    return Field(name, addr, kind, rw, default, lo, hi, persist, group, optional, net)


def axis_param_fields(n: int) -> List[Field]:
    b = AXIS_BASE[n]
    g = f"axis{n}"
    p = lambda off, name, kind, default, lo=None, hi=None: _f(f"X{n}.{name}".replace("X1", "X").replace("X2", "Y"),
                                                           b + off, kind, True, default, lo, hi, True, g)
    return [
        p(0x100, "pulse_mode", "u16", 0, 0, 1), p(0x101, "pulse_logic", "u16", 0, 0, 1),
        p(0x102, "dir_invert", "u16", 0, 0, 1), p(0x103, "dir_setup_us", "u16", 10, 1, 1000),
        p(0x104, "max_speed", "u32", 100000, 1, 4000000), p(0x106, "start_speed", "u32", 0, 0, 4000000),
        p(0x108, "default_accel_ms", "u16", 100, 1, 65535), p(0x109, "default_decel_ms", "u16", 100, 1, 65535),
        p(0x10A, "accel_curve", "u16", 0, 0, 1), p(0x10B, "s_curve_ratio", "u16", 50, 0, 100),
        p(0x10C, "jog_speed", "u32", 10000, 1, 4000000), p(0x10E, "jog_max_speed", "u32", 50000, 1, 4000000),
        p(0x110, "jog_accel_ms", "u16", 100, 1, 65535), p(0x111, "home_method", "u16", 0, 0, 2),
        p(0x112, "home_dir", "u16", 1, 0, 1), p(0x113, "home_speed_high", "u32", 10000, 1, 4000000),
        p(0x115, "home_speed_creep", "u32", 500, 1, 4000000), p(0x117, "home_offset", "s32", 0),
        p(0x119, "home_timeout_s", "u16", 60, 0, 3600), p(0x11A, "swlimit_enable", "u16", 0, 0, 3),
        p(0x11B, "swlimit_pos", "s32", 2147483647), p(0x11D, "swlimit_neg", "s32", -2147483648),
        p(0x11F, "input_logic", "bits", 0x0006), p(0x120, "input_filter_ms", "u16", 1, 0, 20),
        p(0x121, "limit_stop_mode", "u16", 0, 0, 1), p(0x122, "limit_decel_ms", "u16", 10, 1, 65535),
        p(0x123, "alarm_stop_mode", "u16", 0, 0, 1), p(0x124, "inp_wait_enable", "u16", 0, 0, 1),
        p(0x125, "inp_timeout_ms", "u16", 3000, 0, 60000), p(0x126, "svon_at_boot", "u16", 0, 0, 1),
        p(0x127, "estop_pos_policy", "u16", 0, 0, 1), p(0x128, "require_home", "u16", 1, 0, 1),
        p(0x129, "svon_mode", "u16", 0, 0, 1),
        p(0x12A, "pulse_res_hz", "u32", 0),            # default "unconfirmed" (T-09); 0 = not set
        # 5.12
        p(0x140, "unit_mode", "u16", 0, 0, 2), p(0x141, "pulses_per_rev", "u32", 10000, 1, 10000000),
        p(0x143, "lead_um", "u32", 10000, 1, 10000000), p(0x145, "gear_num", "u16", 1, 1, 1000),
        p(0x146, "gear_den", "u16", 1, 1, 1000), p(0x147, "test_rpm", "u16", 60, 1, 300),
    ]


def point_fields(n: int, i: int) -> List[Field]:
    b = POINT_BASE + (n - 1) * 0x80 + i * 0x10
    pre = ("X" if n == 1 else "Y") + f".pt{i}."
    g = "points"
    return [_f(pre + "pos_unit", b, "s32", True, 0, None, None, True, g),
            _f(pre + "speed_unit", b + 2, "u32", True, 0, None, None, True, g),
            _f(pre + "accel_ms", b + 4, "u16", True, 100, None, None, True, g),
            _f(pre + "decel_ms", b + 5, "u16", True, 100, None, None, True, g),
            _f(pre + "valid", b + 6, "u16", True, 0, 0, 1, True, g)]


SYSTEM_SETTINGS = [
    _f("commloss_enable", 0x0110, "u16", True, 0, 0, 1, True, "system"),
    _f("commloss_time_ms", 0x0111, "u16", True, 1000, 100, 60000, True, "system"),
    _f("commloss_scope", 0x0112, "u16", True, 0, 0, 1, True, "system"),
    _f("auto_save", 0x0113, "u16", True, 1, 0, 1, True, "system"),
    _f("max_clients", 0x0114, "u16", True, 4, 1, 16, True, "system"),
    _f("setup_done", 0x0130, "u16", True, 0, 0, 1, True, "system"),
]
# 192.168.0.220 -> 0xC0A800DC
NETWORK = [
    _f("ip_addr", 0x4000, "u32", True, 0xC0A800DC, None, None, True, "network", net=True),
    _f("subnet_mask", 0x4002, "u32", True, 0xFFFFFF00, None, None, True, "network", net=True),
    _f("gateway", 0x4004, "u32", True, 0xC0A80001, None, None, True, "network", net=True),   # [assumed default]
    _f("unit_id", 0x4006, "u16", True, 1, 1, 247, True, "network", net=True),
    _f("rtu_baud", 0x400A, "u16", True, 19200, None, None, True, "network", optional=True, net=True),
    _f("rtu_parity", 0x400B, "u16", True, 0, 0, 2, True, "network", optional=True, net=True),
    _f("rtu_addr", 0x400C, "u16", True, 1, 1, 247, True, "network", optional=True, net=True),
    _f("comm_mode", 0x400E, "u16", True, 0, 0, 1, True, "network", optional=True, net=True),
]
LEGACY = [
    _f("legacy_move_pulses", 0x0000, "u32", True, 1000, 1, 0xFFFFFFFF, True, "legacy"),
    _f("legacy_speed", 0x0002, "u32", True, 10000, 100, 1000000, True, "legacy"),
    _f("legacy_accel_ms", 0x0014, "u32", True, 100, 10, 60000, True, "legacy"),
    _f("legacy_decel_ms", 0x0016, "u32", True, 100, 10, 60000, True, "legacy"),
]


def axis_runtime_fields(n: int) -> List[Field]:
    b = AXIS_BASE[n]
    r = lambda off, name, kind, rw=False, default=0, lo=None, hi=None: _f(f"A{n}.{name}", b + off, kind, rw, default, lo, hi)
    return [
        r(A_STATE, "axis_state", "u16"), r(A_STATUS, "axis_status", "bits"), r(A_CMDPOS, "cmd_position", "s32"),
        r(A_CMDSPEED, "cmd_speed", "s32"), r(A_TARGET_ACT, "target_position_act", "s32"), r(A_ALARM, "alarm_code", "u16"),
        r(A_STOPCAUSE, "last_stop_cause", "u16"), r(A_INPUTS, "axis_inputs", "bits"), r(A_CMDRES, "cmd_result", "u16"),
        r(A_MOVECOUNT, "move_count", "u32"), r(A_PULSES_DONE, "move_pulses_done", "u32"),
        r(A_POSUNIT, "position_unit", "s32"), r(A_MAXRPM, "max_rpm_calc", "u16"),
        r(A_CMD, "axis_command", "u16", True), r(A_TARGET, "target_position", "s32", True),
        r(A_SPEED, "move_speed", "u32", True, 10000, 0, 4000000), r(A_ACCEL, "accel_time_ms", "u16", True, 100, 10, 60000),
        r(A_DECEL, "decel_time_ms", "u16", True, 100, 10, 60000), r(A_JOGOVR, "jog_speed_override", "u32", True),
        r(A_TARGET_UNIT, "target_unit", "s32", True), r(A_SPEED_UNIT, "speed_unit", "u32", True),
        r(A_POINT_INDEX, "point_index", "u16", True, 0, 0, POINT_COUNT - 1),
        r(A_ALARM2, "alarm_code2", "u16"), r(A_ALARM_COUNT, "alarm_count", "u16"),
    ]


SYSTEM_RUNTIME = [
    _f("model_id", MODEL_ID, "u16", False), _f("axis_count", AXIS_COUNT, "u16", False),
    _f("fw_version", FW_VERSION, "u32", False), _f("regmap_version", REGMAP_VER, "u16", False),
    _f("sys_status", SYS_STATUS, "bits", False), _f("estop_state", ESTOP_STATE, "u16", False),
    _f("tcp_clients", TCP_CLIENTS, "u16", False), _f("uptime_s", UPTIME_S, "u32", False),
    _f("heartbeat", HEARTBEAT, "u16", False), _f("sys_inputs", SYS_INPUTS, "bits", False),
    _f("reset_reason", RESET_REASON, "u16", False), _f("boot_status", BOOT_STATUS, "bits", False),
    _f("comm_mode_active", COMM_MODE_ACTIVE, "u16", False), _f("alarm_axis_mask", ALARM_AXIS_MASK, "bits", False),
    _f("di_raw", DI_RAW, "bits", False), _f("do_state", DO_STATE, "bits", False),
    _f("wiring_status", WIRING_STATUS, "bits", False), _f("alarm_hint", ALARM_HINT, "u16", False),
    _f("sys_command", SYS_COMMAND, "u16", True), _f("sys_cmd_result", SYS_CMD_RESULT, "u16", False),
    _f("unlock_key", UNLOCK_KEY_ADDR, "u16", True), _f("user_do", USER_DO, "bits", True, 0, 0, 3),
    _f("net_apply", NET_APPLY, "u16", True, 0, 0, 1, False, "", False, True),
    _f("hist_count", HIST_COUNT, "u16", False), _f("hist_latest_index", HIST_COUNT + 1, "u16", False),
    _f("mac_addr", 0x4007, "u16", False),
    _f("mac_addr1", 0x4008, "u16", False), _f("mac_addr2", 0x4009, "u16", False),
]
HIST_N = 32


def backup_fields(axis_count: int) -> List[Field]:
    out = list(SYSTEM_SETTINGS)
    for n in range(1, axis_count + 1):
        out += axis_param_fields(n)
    for n in range(1, axis_count + 1):
        for i in range(POINT_COUNT):
            out += point_fields(n, i)
    out += NETWORK + LEGACY
    return out


def all_fields(axis_count: int) -> List[Field]:
    out = list(SYSTEM_RUNTIME) + backup_fields(axis_count) + LEGACY_RUNTIME + hist_fields()
    for n in range(1, axis_count + 1):
        out += axis_runtime_fields(n)
    return out


def runs(fields: List[Field]):
    """Group address-contiguous fields into read runs (max 125 registers)."""
    fs = sorted(fields, key=lambda f: f.addr)
    cur = []
    for f in fs:
        if cur and (cur[-1].addr + cur[-1].words != f.addr or sum(x.words for x in cur) + f.words > 125
                    or cur[-1].optional != f.optional):
            yield cur
            cur = []
        cur.append(f)
    if cur:
        yield cur


def fw_version_str(raw: int) -> str:
    return f"{raw >> 16}.{(raw >> 8) & 0xFF}.{raw & 0xFF}"


# ---- legacy window runtime (5.0.1) and alarm history (5.9); used by the simulator -------------------
LEGACY_RUNTIME = [
    _f("legacy_current", 0x0006, "u32", False), _f("legacy_status", 0x000A, "bits", False),
    _f("legacy_result", 0x000D, "u16", False), _f("legacy_fault", 0x000E, "u16", False),
    _f("legacy_clients", 0x000F, "u16", False), _f("legacy_link", 0x0010, "u16", False),
    _f("legacy_start_speed", 0x0012, "u32", False),
]


def hist_fields() -> List[Field]:
    out = []
    for i in range(HIST_N):
        b = HIST_BASE + 4 * i
        out += [_f(f"hist{i}.code", b, "u16", False), _f(f"hist{i}.axis", b + 1, "u16", False),
                _f(f"hist{i}.uptime_s", b + 2, "u32", False)]
    return out
