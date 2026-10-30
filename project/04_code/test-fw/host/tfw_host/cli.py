"""Command line: python -m tfw_host.cli run scenario.yaml --target 192.168.0.220:502 | report results out.md"""
import argparse
import json
import sys

from .report import make_report
from .scenario import run_scenario


def main(argv=None):
    ap = argparse.ArgumentParser(prog="tfw_host")
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("scenario")
    r.add_argument("--target", default="192.168.0.220:502")
    r.add_argument("--results", default="results")
    p = sub.add_parser("report")
    p.add_argument("results")
    p.add_argument("out")
    a = ap.parse_args(argv)
    if a.cmd == "run":
        res = run_scenario(a.scenario, a.target, a.results)
        print(json.dumps({k: [x["verdict"] for x in v] for k, v in res.items()}, ensure_ascii=False, indent=1))
    else:
        make_report(a.results, a.out)
        print("written", a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
