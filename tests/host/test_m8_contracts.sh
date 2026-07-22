#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
sbi_dts=$root/platform/quard-star/dts/quard_star_sbi.dts
m8_dts=$root/platform/quard-star/dts/quard_star_sbi_m8.dts
trusted_makefile=$root/trusted/Makefile
trusted_config=$root/trusted/FreeRTOSConfig.h

for port_file in port.c portASM.S portContext.h portmacro.h \
  freertos_risc_v_chip_specific_extensions.h; do
  test -f "$root/trusted/port/$port_file"
done
grep -Fq '$(TOP_DIR)/port/*.c' "$trusted_makefile"
grep -Fq '$(TOP_DIR)/port/*.S' "$trusted_makefile"
grep -Fq -- '-march=rv64ima_zicsr_zifencei' "$trusted_makefile"
grep -Fq -- '-mabi=lp64' "$trusted_makefile"
grep -Fq 'configMTIME_BASE_ADDRESS' "$trusted_config"
grep -Eq 'configMTIME_BASE_ADDRESS[[:space:]]+\([[:space:]]*0[[:space:]]*\)' \
  "$trusted_config"
grep -Fq 'sbi_set_timer' "$root/trusted/port/port.c"
grep -Fq 'scause' "$root/trusted/port/portASM.S"
grep -Fq 'sepc' "$root/trusted/port/portASM.S"
grep -Fq 'sstatus' "$root/trusted/port/portASM.S"
grep -Fq '#define portYIELD() __asm volatile( "ebreak" );' \
  "$root/trusted/port/portmacro.h"
grep -Fq 'li t0, 3' "$root/trusted/port/portASM.S"
if grep -Eq '\b(mcause|mepc|mstatus|mtvec|mie|mhartid)\b' \
    "$root"/trusted/port/*; then
  echo 'FAIL: trusted port still uses machine-mode CSRs' >&2
  exit 1
fi

grep -Fq 'possible-harts = <&cpu0 &cpu1 &cpu2 &cpu3 &cpu4 &cpu5 &cpu6>;' \
  "$m8_dts"
grep -Fq 'possible-harts = <&cpu7>;' \
  "$m8_dts"
grep -Fq 'next-addr = <0 0xbf800000>;' \
  "$m8_dts"
grep -Fq 'trusted_mem: trusted-memory' "$sbi_dts"
grep -Fq 'base = <0 0xbf800000>;' "$sbi_dts"
grep -Fq 'order = <23>;' "$sbi_dts"
grep -Fq 'trusted_uart: trusted-uart' "$sbi_dts"
grep -Fq 'base = <0 0x10002000>;' "$sbi_dts"
grep -Fq 'regions = <&trusted_mem 0x0>, <&trusted_uart 0x0>, <&allmem 0x7>;' \
  "$m8_dts"
grep -Fq 'regions = <&trusted_mem 0x7>, <&trusted_uart 0x3>;' "$m8_dts"
for hart in 1 2 3 4 5 6; do
  grep -Fq "cpu$hart: cpu@$hart" \
    "$root/platform/quard-star/dts/quard_star_kernel_m8.dts"
done
grep -Fq 'QS_SMP=8' "$root/scripts/m8-smoke.sh"
grep -Fq 'QS:TRUSTED_READY' "$root/scripts/m8-smoke.sh"
grep -Fq 'QS:TRUSTED_SCHED_OK' "$root/scripts/m8-smoke.sh"
grep -Fq 'QS:PMP_UNTRUSTED_DENY_OK' "$root/scripts/m8-smoke.sh"
grep -Fq 'QS:PMP_TRUSTED_DENY_OK' "$root/scripts/m8-smoke.sh"
for access in LOAD STORE EXEC; do
  grep -Fq "QS:PMP_UNTRUSTED_${access}_DENY_OK" \
    "$root/kernel/src/selftest.c"
done
grep -Fq 'QS:PMP_UNTRUSTED_${access}_DENY_OK' "$root/scripts/m8-smoke.sh"
grep -Fq 'm9_pmp_handle_fault' "$root/kernel/include/timeros/selftest.h"
grep -Fq 'm9_pmp_handle_fault' "$root/kernel/src/trap.c"
grep -Fq '#define EXC_INST_ACCESS  1' "$root/kernel/include/timeros/riscv.h"
grep -Fq '#define EXC_LOAD_ACCESS  5' "$root/kernel/include/timeros/riscv.h"
grep -Fq '#define EXC_STORE_ACCESS 7' "$root/kernel/include/timeros/riscv.h"
grep -Fq 'PTE_R | PTE_W | PTE_X' "$root/kernel/src/address.c"
grep -Fq 'QS:HART_ONLINE:7' "$root/scripts/m8-smoke.sh"
grep -Fq 'QS_M9_PMP_TEST' "$root/scripts/m8-build.sh"
grep -Fq 'QS_M7E_TEST' "$root/scripts/m8-build.sh"
grep -Fq 'QS:M7E_TFTP_1M_OK' "$root/scripts/m5-smoke.sh"
grep -Fq 'rm -f "$root/out/m8/disk/disk.img"' "$root/scripts/m8-build.sh"
grep -Fq 'QS:TEST_PASS:m8-smoke' "$root/kernel/src/selftest.c"
echo 'PASS: M8 trusted-domain and seven-hart contracts'
