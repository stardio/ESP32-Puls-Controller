#!/bin/sh
# Risk 2 check (run on the user's PC after the build; UNTESTED here because no ESP-IDF toolchain in the authoring container).
# Usage: tools/check_iram.sh build_on/tfw_p4_pretest.elf
# 1) every ISR-path function must be linked into IRAM (address 0x4FFxxxxx on ESP32-P4 [assumed range]);
# 2) their call targets must not be flash addresses (0x40000000-0x4BFFFFFF [assumed range]).
ELF=${1:?elf}
NM=riscv32-esp-elf-nm; OD=riscv32-esp-elf-objdump
for f in on_reach update_all on_gpt_alarm es_isr tfw_pulse2_stop tfw_pulse2_count_isr axis_total set_ticks; do
  $NM "$ELF" | grep -E " [tT] ($f|.*_$f)\$" || echo "NOT FOUND (maybe inlined): $f"
done
echo "--- call targets of ISR functions (look for flash addresses 0x40..-0x4b..) ---"
for f in on_reach update_all on_gpt_alarm es_isr tfw_pulse2_stop; do
  $OD -d --no-show-raw-insn "$ELF" | awk -v fn="<$f>:" '$0 ~ fn {p=1} p && /^$/ {p=0} p && /jal|call|tail/ {print}' 
done
