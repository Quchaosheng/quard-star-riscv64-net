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

require_order() {
  file=$1
  first=$2
  second=$3
  message=$4
  first_line=$(grep -Fn "$first" "$root/$file" 2>/dev/null | head -1 | cut -d: -f1 || true)
  second_line=$(grep -Fn "$second" "$root/$file" 2>/dev/null | head -1 | cut -d: -f1 || true)
  if [ -z "$first_line" ] || [ -z "$second_line" ] || [ "$first_line" -ge "$second_line" ]; then
    echo "FAIL: $message" >&2
    status=1
  fi
}

require_text kernel/include/timeros/task.h \
  'Sleeping,' 'task state must represent sleeping processes'
require_text kernel/include/timeros/task.h \
  'void *wait_channel;' 'sleeping tasks need a wait channel'
require_text kernel/include/timeros/task.h \
  'u64 wait_deadline;' 'sleeping tasks need an absolute deadline'
require_text kernel/include/timeros/task.h \
  'struct semaphore child_exit;' 'process wait must use a child-exit semaphore'
require_text kernel/include/timeros/wait.h \
  'struct wait_queue {' 'M2C must expose wait queues'
require_text kernel/include/timeros/wait.h \
  'struct semaphore {' 'M2C must expose counting semaphores'
require_text kernel/include/timeros/wait.h \
  'struct sleeplock {' 'M2C must expose sleeping locks'
require_text kernel/src/wait.c \
  'task_sleep(' 'wait primitives must block through the scheduler'
require_text kernel/src/task.c \
  'task_wake_expired_locked' 'the scheduler must wake timed-out tasks'
require_text kernel/src/task.c \
  'printk("QS:WAIT_OK\n");' 'process wait needs a stable completion marker'
require_text kernel/src/task.c \
  'printk("QS:SEM_TIMEOUT_OK\n");' 'timed semaphore wait needs runtime coverage'
require_text kernel/include/timeros/sbi.h \
  'struct sbiret sbi_send_ipi(u64 hart_mask, u64 hart_mask_base);' \
  'scheduler wakeup must use SBI IPI'
require_text kernel/include/timeros/sbi.h \
  'struct sbiret sbi_remote_sfence_vma(u64 hart_mask, u64 hart_mask_base,' \
  'page reclamation must use SBI RFENCE'
require_text kernel/src/trap.c \
  'case IRQ_S_SOFT:' 'the kernel must handle supervisor software interrupts'
require_text kernel/src/trap.c \
  'clear_sip(SIP_SSIP);' 'software interrupt handling must clear SSIP'
require_text kernel/src/trap.c \
  'printk("QS:IPI_OK\n");' 'the receiving IPI path needs a stable marker'
require_text kernel/src/address.c \
  'void tlb_shootdown_all(void)' 'M2C must provide TLB shootdown'
require_order kernel/src/address.c \
  'tlb_shootdown_all();' 'uvmunmap(pagetable' \
  'shootdown must complete before user mappings are removed'
require_text kernel/src/address.c \
  'printk("QS:RFENCE_OK\n");' 'completed remote fencing needs a stable marker'
require_text patches/qemu/0001-add-quard-star-machine.patch \
  'sifive_test_create(quard_star_memmap[QUARD_STAR_TEST].base);' \
  'the quard-star machine must instantiate QEMU test exit'
require_text platform/quard-star/dts/quard_star_kernel.dts \
  'test@100000 {' 'the kernel DTB must describe QEMU test exit'
require_text kernel/src/selftest.c \
  'QS:TEST_PASS:m2c-smoke' 'M2C quick completion needs a marker'
require_text scripts/m2c-build.sh \
  'export QS_STAGE=m2c' 'M2C quick build needs isolated output'
require_text scripts/m2c-stress.sh \
  'QS_MIGRATION_TARGET=10000' 'M2C stress must require 10000 migrations'
require_text scripts/m2c-stress.sh \
  'QS_ALLOC_ITERATIONS=50000' 'two stress workers must total 100000 page loops'
require_text scripts/m2c-stress.sh \
  'QS_STRESS_MIN_TICKS=1200000000ULL' 'stress must run for at least 120 seconds'

if [ "$status" -ne 0 ]; then
  exit "$status"
fi

echo "PASS: M2C source contracts"
