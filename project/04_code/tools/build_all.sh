#!/usr/bin/env bash
# Build-only smoke test of both firmware projects (run after export.sh).
# Usage: bash project/04_code/tools/build_all.sh   (from repo root)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOG="$ROOT/tools/build_all.log"; : > "$LOG"
run() { echo "== $*" | tee -a "$LOG"; "$@" >>"$LOG" 2>&1 && echo "   OK" || { echo "   FAIL (see $LOG)"; FAILED=1; }; }
FAILED=0

cd "$ROOT/test-fw"
run idf.py -B build_on  -D SDKCONFIG=build_on/sdkconfig  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.tfw_iram_on"  set-target esp32p4 build
run idf.py -B build_off -D SDKCONFIG=build_off/sdkconfig -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.tfw_iram_off" set-target esp32p4 build

cd "$ROOT/firmware"
for m in 1axis_tcp 1axis_rtu 2axis_tcp 2axis_rtu; do
  run idf.py -B "build_$m" -D SDKCONFIG="build_$m/sdkconfig" \
    -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.model_$m" set-target esp32p4 build
done
exit $FAILED
