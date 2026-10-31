"""Command line: python -m tfw_host.cli run scenario.yaml --target 192.168.0.220:502 | report results out.md"""
import argparse
import json
import os
import sys

from .report import make_report
from .scenario import judge_file, run_scenario


def main(argv=None):
    ap = argparse.ArgumentParser(prog="tfw_host")
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("scenario")
    r.add_argument("--target", default="192.168.0.220:502")
    r.add_argument("--results", default="results")
    r.add_argument("--wait-capture", action="store_true",
                   help="after each step with la_csv: stop, let the operator export the capture, then judge (N-04)")
    j = sub.add_parser("judge", help="attach a logic-analyser CSV to an earlier run (PENDING) and judge it")
    j.add_argument("result_json")
    j.add_argument("csv")
    t = sub.add_parser("t15", help="T-15: inject a CPU stop during output, then read reset evidence (build with TFW_T15_FAULT_INJECT=y)")
    t.add_argument("--target", default="192.168.0.220:502")
    t.add_argument("--results", default="results")
    t.add_argument("--mode", choices=["task", "irqoff"], default="task")
    t.add_argument("--freq", type=int, default=200000)
    t.add_argument("--count", type=int, default=40000000)
    p = sub.add_parser("report")
    p.add_argument("results")
    p.add_argument("out")
    a = ap.parse_args(argv)
    if a.cmd == "run":
        hook = None
        if a.wait_capture:
            def hook(step, rec):
                input(f"[{rec['id']}] 운전 종료. 이번 실행의 캡처를 내보낸 뒤 Enter (기본 경로 {step['la_csv']}) ")
                return step["la_csv"] if os.path.isabs(step["la_csv"]) else os.path.join(step.get("_base_dir", "."), step["la_csv"])
        res = run_scenario(a.scenario, a.target, a.results, capture=hook)
        print(json.dumps({k: [x["verdict"] for x in v] for k, v in res.items()}, ensure_ascii=False, indent=1))
    elif a.cmd == "t15":
        from .t15 import run_t15
        rec = run_t15(a.target, a.results, a.mode, a.freq, a.count)
        print(json.dumps({k: v for k, v in rec.items() if k != "_path"}, ensure_ascii=False, indent=1), "->", rec["_path"])
    elif a.cmd == "judge":
        rec = judge_file(a.result_json, a.csv)
        print(json.dumps(rec["verdict"], ensure_ascii=False, indent=1))
    else:
        make_report(a.results, a.out)
        print("written", a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
