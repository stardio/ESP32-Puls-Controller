#!/bin/sh
# Risk 2 check (run on the user's PC after the build; UNTESTED here because no ESP-IDF toolchain in the authoring container).
# Usage: tools/check_iram.sh build_on/tfw_p4_pretest.elf
# 1) every ISR-path function must be linked into IRAM (address 0x4FFxxxxx on ESP32-P4 [assumed range]);
# 2) their call targets must not be flash addresses (0x40000000-0x4BFFFFFF [assumed range]).
ELF=${1:?elf}
NM=riscv32-esp-elf-nm; OD=riscv32-esp-elf-objdump
# M-01: tfw_es_trip / tfw_upd_record / tfw_profile_freq are always_inline: "NOT FOUND (maybe inlined)" is the expected answer.
# If one of them IS found, it must be in IRAM. Driver functions called from ISRs are listed too (iram_on build: all must be IRAM).
for f in on_reach update_all on_gpt_alarm es_isr tfw_pulse2_stop tfw_pulse2_count_isr axis_total set_ticks tfw_es_trip tfw_upd_record tfw_profile_freq tfw_estop_input_active pcnt_unit_get_count mcpwm_generator_set_force_level mcpwm_timer_start_stop mcpwm_timer_set_period mcpwm_comparator_set_compare_value gpio_set_level; do
  $NM "$ELF" | grep -E " [tT] ($f|.*_$f)\$" || echo "NOT FOUND (maybe inlined): $f"
done
# n-3: tfw_pulse2_stop (called from es_isr) calls mcpwm_timer_start_stop. If it is NOT in IRAM in the iram_on build,
# path A must use force-level only and leave the timer stop to a task (decision after the first run).
echo "--- call targets of ISR functions (look for flash addresses 0x40..-0x4b..) ---"
for f in on_reach update_all on_gpt_alarm es_isr tfw_pulse2_stop tfw_pulse2_count_isr; do
  $OD -d --no-show-raw-insn "$ELF" | awk -v fn="<$f>:" '$0 ~ fn {p=1} p && /^$/ {p=0} p && /jal|call|tail/ {print}' 
done
