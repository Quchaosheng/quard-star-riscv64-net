#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
status=0

require_text() {
  file=$1
  text=$2
  message=$3
  if ! grep -Fq "$text" "$root/$file"; then
    echo "FAIL: $message" >&2
    status=1
  fi
}

require_text kernel/src/syscall.c \
  'static char *translate_user_ptr(const char *uaddr, u64 required)' \
  'user translation must receive required PTE permissions'
require_text kernel/src/syscall.c \
  'if (start_va >= MAXVA)' \
  'user translation must reject out-of-range virtual addresses'
require_text kernel/src/syscall.c \
  'translate_user_ptr(src + i, PTE_R)' \
  'copy_from_user must require a readable mapping'
require_text kernel/src/syscall.c \
  'translate_user_ptr(dst + i, PTE_W)' \
  'copy_to_user must require a writable mapping'
require_text kernel/include/timeros/syscall.h \
  'reg_t __SYSCALL(size_t syscall_id, reg_t arg1, reg_t arg2, reg_t arg3);' \
  'syscall dispatch must return a register-width value'
require_text kernel/src/trap.c \
  'reg_t result = __SYSCALL(cx->a7, cx->a0, cx->a1, cx->a2);' \
  'the trap path must preserve register-width syscall results'
require_text kernel/src/timer.c \
  'return r_mtime() / (CLOCK_FREQ / 1000000ULL);' \
  'get_time_us must return microseconds'
require_text kernel/src/task.c \
  'kfree(floor_phys(phys_addr_from_size_t(p->trap_cx_ppn)));' \
  'final process cleanup must free the trap frame'
require_text kernel/src/virtio_disk.c \
  '*VTMO_REG(VIRTIO_MMIO_QUEUE_ALIGN) = PAGE_SIZE;' \
  'VirtIO MMIO v1 must configure queue alignment'
require_text kernel/src/virtio_disk.c \
  '*VTMO_REG(VIRTIO_MMIO_QUEUE_PFN) = (u32)((u64)disk.pages >> PAGE_SIZE_BITS);' \
  'VirtIO MMIO v1 must register the queue PFN'
require_text kernel/src/virtio_disk.c \
  'void virtio_disk_smoke_test()' \
  'the kernel must perform a real block-I/O smoke test'

if [ "$status" -ne 0 ]; then
  exit "$status"
fi

echo "PASS: M1 kernel contracts"
