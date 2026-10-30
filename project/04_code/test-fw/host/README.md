# tfw_host (TFW-05)
Run from `test-fw/host`:
- `python -m tfw_host.cli run scenario_example.yaml --target 192.168.0.220:502 --results results`
- `python -m tfw_host.cli report results reports/report_G3_pretest.md`
Optional `results/meta.json`: `{"firmware_hash": "...", "sdkconfig_diff": "...", "date_equipment": "...", "chip_rev": 301}`.
Needs Python 3.10+; PyYAML only for .yaml scenarios (JSON works without).
