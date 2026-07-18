#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
status=0

require_text() {
  file=$1
  text=$2
  message=$3
  if ! grep -Fq "$text" "$root/$file" 2>/dev/null; then
    echo "FAIL: $message" >&2
    status=1
  fi
}

require_absence() {
  file=$1
  text=$2
  message=$3
  if grep -Fq "$text" "$root/$file" 2>/dev/null; then
    echo "FAIL: $message" >&2
    status=1
  fi
}

require_text platform/quard-star/dts/quard_star_sbi_m2.dts \
  'cpu1: cpu@1 {' 'OpenSBI M2 DTB must describe hart 1'
require_text platform/quard-star/dts/quard_star_sbi_m2.dts \
  'possible-harts = <&cpu0 &cpu1>;' 'the untrusted domain must own hart 0/1'
require_text platform/quard-star/dts/quard_star_kernel_m2.dts \
  'cpu1: cpu@1 {' 'kernel M2 DTB must expose hart 1'
require_text platform/quard-star/boot/start.s \
  'lr.w.aq t1, (t0)' 'secondary harts must acquire firmware readiness'
require_absence platform/quard-star/boot/start.s \
  'amoswap.w.rl zero, t1, (t0)' 'hart 1 must not race hart 0 into OpenSBI cold boot'
require_text patches/opensbi/0001-add-quard-star-platform.patch \
  '__atomic_store_n((u32 *)0x8000, 1, __ATOMIC_RELEASE);' \
  'OpenSBI cold boot must release secondary harts after winning the lottery'
require_text kernel/include/timeros/cpu.h \
  '#define MAX_CPUS 7' 'per-CPU storage must cover hart 0-6'
require_text kernel/src/entry.S \
  'call kernel_entry' 'assembly entry must preserve OpenSBI boot arguments'
require_text kernel/src/cpu.c \
  '#include <libfdt.h>' 'CPU discovery must use the locked libfdt source'
require_text kernel/lib/string.c \
  'int memcmp(const void *lhs, const void *rhs, size_t count)' \
  'libfdt needs the kernel memcmp implementation'
require_text kernel/lib/string.c \
  'void *memmove(void *dest, const void *src, size_t count)' \
  'libfdt needs the kernel memmove implementation'
require_text kernel/lib/string.c \
  'size_t strnlen(const char *str, size_t max)' \
  'libfdt needs the kernel strnlen implementation'
require_text kernel/src/sbi.c \
  'struct sbiret sbi_hart_start(u64 hartid, u64 start_addr, u64 opaque)' \
  'secondary startup must use SBI HSM'
require_text kernel/src/cpu.c \
  '__ATOMIC_RELEASE' 'secondary online publication must use release ordering'
require_text kernel/src/cpu.c \
  '__ATOMIC_ACQUIRE' 'boot hart online checks must use acquire ordering'
require_text kernel/src/address.c \
  'struct run *freelist;' 'the allocator must use an intrusive free-page list'
require_text kernel/src/address.c \
  'struct spinlock lock;' 'the free-page list must be protected by a spinlock'
require_text kernel/src/spinlock.c \
  '__atomic_store_n(&lock->owner, cpu_this(), __ATOMIC_RELAXED);' \
  'spinlock ownership diagnostics must not introduce a data race'
require_absence kernel/src/address.c \
  'Stack recycled;' 'the fixed recycled-page array must be removed'
require_text kernel/src/main.c \
  'printk("QS:SMP_ALLOC_OK\n");' 'the concurrent allocator test needs a marker'

if [ "$status" -ne 0 ]; then
  exit "$status"
fi

echo "PASS: M2A source contracts"
