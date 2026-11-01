"""Alarm code -> Korean cause/action. Source of truth: project/03_design/hmi/alarm-messages.md when it exists
(table rows starting with a 0x code: | code | name | cause | action |). Otherwise the DRAFT table below
(codes from interface-spec 5.8; wording is a draft written by the tool author)."""
import os
import re

DRAFT = {
    0x0000: ("없음", "알람이 없습니다.", "조치 필요 없음."),
    0x0101: ("E-ESTOP 비상정지", "STOP(비상정지) 입력이 눌렸거나 STOP 배선이 끊어졌습니다.",
             "STOP 버튼을 풀고 배선(NC 접점 닫힘)을 확인한 뒤 START(운전 시작)를 보내면 해제됩니다."),
    0x0201: ("E-LIMP +리밋", "+방향 리밋 센서가 감지되었습니다.", "반대(-) 방향으로 이동해 리밋에서 벗어나세요. 센서 배선·극성(NC/NO)을 확인하세요."),
    0x0202: ("E-LIMN -리밋", "-방향 리밋 센서가 감지되었습니다.", "반대(+) 방향으로 이동해 리밋에서 벗어나세요. 센서 배선·극성(NC/NO)을 확인하세요."),
    0x0203: ("E-SWLP +소프트 리밋", "+방향 소프트 리밋 위치를 넘으려 했습니다.", "반대 방향으로 이동하거나 소프트 리밋 설정을 확인하세요."),
    0x0204: ("E-SWLN -소프트 리밋", "-방향 소프트 리밋 위치를 넘으려 했습니다.", "반대 방향으로 이동하거나 소프트 리밋 설정을 확인하세요."),
    0x0301: ("E-DRVALM 드라이브 알람", "서보 드라이브가 알람 신호를 보냈습니다.", "드라이브 표시부의 알람 코드를 확인하고 원인을 해소한 뒤 알람 해제를 누르세요."),
    0x0401: ("E-COMMLOSS 통신 두절", "설정 시간 동안 HMI/PC와 통신이 끊겼습니다.", "케이블·IP 설정을 확인한 뒤 알람 해제를 누르세요."),
    0x0402: ("E-JOGKEEP JOG 유지 신호 상실", "JOG 버튼 유지 신호가 끊겨 정지했습니다.", "자동 해제됩니다. 통신 상태를 확인하세요."),
    0x0501: ("E-HOMEFAIL 원점복귀 실패", "제한 시간 안에 원점 센서를 찾지 못했거나 리밋에 먼저 닿았습니다.", "원점 센서 배선·방향·속도 설정을 확인하고 알람 해제 후 다시 시도하세요."),
    0x0502: ("E-INPTO 완료 입력 시간 초과", "드라이브 위치결정 완료(INP) 신호가 제한 시간 안에 오지 않았습니다.", "INP 배선과 드라이브 게인·부하를 확인하고 알람 해제를 누르세요."),
    0x0601: ("E-PE-UNDERRUN 펄스 구간 마감 실패", "펄스 출력 준비가 늦어 출력이 중단되었습니다.", "속도·가감속 값을 낮추고 알람 해제 후 다시 시도하세요. 반복되면 제조사에 문의하세요."),
    0x0602: ("E-PE-HW 펄스 엔진 오류", "펄스 출력 하드웨어 계수가 맞지 않습니다.", "전원을 껐다 켜세요. 반복되면 제조사에 문의하세요."),
    0x0701: ("E-PARAM 파라미터 손상", "저장된 설정이 손상되어 기본값이 적재되었습니다.", "설정을 다시 입력(또는 백업 복원)하고 저장하세요."),
    0x0702: ("E-NVM 저장 실패", "설정을 비휘발 메모리에 저장하지 못했습니다.", "알람 해제 후 다시 저장하세요. 반복되면 제조사에 문의하세요."),
    0x0801: ("E-WDT 워치독 리셋 이력", "워치독 리셋이 발생한 이력이 있습니다.", "기록용입니다. 반복되면 제조사에 문의하세요."),
}

_ROW = re.compile(r"^\|\s*(0x[0-9A-Fa-f]+)\s*\|(.*)\|\s*$")


def parse_markdown(text: str):
    table = {}
    for line in text.splitlines():
        m = _ROW.match(line.strip())
        if not m:
            continue
        cells = [c.strip() for c in m.group(2).split("|")]
        if len(cells) >= 3:
            table[int(m.group(1), 16)] = (cells[0], cells[1], cells[2])
    return table


def default_path():
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.normpath(os.path.join(here, "..", "..", "..", "03_design", "hmi", "alarm-messages.md"))


def load(path=None):
    table = dict(DRAFT)
    path = path or default_path()
    if os.path.isfile(path):
        try:
            with open(path, encoding="utf-8") as f:
                table.update(parse_markdown(f.read()))
        except OSError:
            pass
    return table


def describe(code: int, table=None):
    table = table or load()
    if code in table:
        return table[code]
    return (f"미등록 코드 0x{code:04X}", "문구표에 없는 알람 코드입니다.", "제조사 문서를 확인하거나 문의하세요.")
