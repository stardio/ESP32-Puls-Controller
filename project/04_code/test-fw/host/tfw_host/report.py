"""Report generation (TFW-05): report_G3_pretest.md with all 14 T-IDs."""
import glob
import json
import os
import time

from .judge import ALL_IDS, T_TITLES, SUBSTITUTED_IDS, HARDWARE_ONLY, SUBSTITUTED, NOT_RUN

LABEL = {"PASS": "합격", "FAIL": "불합격", "RECORD": "기록(판정 없음)", "SUBSTITUTED": "기존 실적으로 대체(D-028)",
         "NOT_RUN": "미실시"}


def _load(results_dir):
    latest = {}
    for p in sorted(glob.glob(os.path.join(results_dir, "T-*.json"))):
        with open(p, encoding="utf-8") as f:
            rec = json.load(f)
        latest.setdefault(rec["id"], []).append((p, rec))
    meta = {}
    mp = os.path.join(results_dir, "meta.json")
    if os.path.exists(mp):
        with open(mp, encoding="utf-8") as f:
            meta = json.load(f)
    return latest, meta


def make_report(results_dir: str, out_md: str) -> None:
    runs, meta = _load(results_dir)
    L = ["# G3 전 실측 보고서 (시험 펌웨어, D-025)", "",
         f"- 생성 시각: {time.strftime('%Y-%m-%d %H:%M:%S')}",
         f"- 펌웨어 빌드 해시: {meta.get('firmware_hash', '미확인')}",
         f"- sdkconfig 차이(IRAM 설정 등): {meta.get('sdkconfig_diff', '미확인')}",
         f"- 칩 리비전(0x7036): {meta.get('chip_rev', _first(runs, 'chip_rev', '미확인'))}",
         f"- 시험 일시·장비: {meta.get('date_equipment', '미확인')}", "",
         "## T-ID별 결과", "", "| T-ID | 항목 | 결과 | 근거·측정값 | 미확인 사항 |", "|---|---|---|---|---|"]
    for tid in ALL_IDS:
        status, evid, unk = NOT_RUN, "", ""
        if tid in SUBSTITUTED_IDS:
            status, evid = SUBSTITUTED, SUBSTITUTED_IDS[tid]
            unk = "측정 기록(파형 캡처) 첨부는 미확인"
        elif tid == "T-14" and tid not in runs:
            status = SUBSTITUTED
            evid = "1축: 기존 실적으로 대체(D-028, 1 MHz 모터 운전 보고; servo1axis CURRENT_STATE.md). 2축: 미실시"
            unk = "드라이브 모델·배선 형식 미확인; 2축 실측 없음"
        elif tid in runs:
            order = {"FAIL": 0, "NOT_RUN": 1, "RECORD": 2, "PASS": 3}
            vs = [r["verdict"]["status"] for _, r in runs[tid]]
            status = min(vs, key=lambda s: order.get(s, 1))
            last = runs[tid][-1][1]
            evid = f"{len(runs[tid])}건 실행; " + "; ".join(last["verdict"].get("reasons", []))
            d = last.get("data", {})
            if d:
                evid += f" [seq_done={d.get('seq_done')}, err={d.get('err_ax1')}/{d.get('err_ax2')}, upd_late={d.get('upd_late')}]"
        elif tid in HARDWARE_ONLY:
            unk = "하드웨어 시제 필요, 미실시"
        L.append(f"| {tid} | {T_TITLES[tid]} | {LABEL.get(status, status)} | {evid} | {unk} |")
    L += ["", "## 원자료", ""]
    for tid in ALL_IDS:
        for p, r in runs.get(tid, []):
            L.append(f"- {tid}: `{os.path.basename(p)}` ({r['verdict']['status']})")
    L += ["", "## 비고", "",
          "- 합격 기준은 architecture 6.1 표를 그대로 적용했다. 기준이 '기록'인 항목은 판정 없이 값만 보고한다.",
          "- 감시점 ISR 지연(0x702E 등)은 펌웨어 내부 추정값(1펄스 분해능)이다. 정밀 값은 로직 분석기 측정을 쓴다."]
    os.makedirs(os.path.dirname(os.path.abspath(out_md)), exist_ok=True)
    with open(out_md, "w", encoding="utf-8") as f:
        f.write("\n".join(L) + "\n")


def _first(runs, key, default):
    for rs in runs.values():
        for _, r in rs:
            v = r.get("data", {}).get(key)
            if v is not None:
                return v
    return default
