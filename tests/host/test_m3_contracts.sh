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

require_absent() {
  file=$1
  text=$2
  message=$3
  if grep -Fq "$text" "$root/$file" 2>/dev/null; then
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

require_text kernel/include/timeros/virtqueue.h \
  'struct virtqueue {' 'M3 needs a reusable split-ring object'
require_text kernel/include/timeros/virtio_mmio.h \
  'struct virtio_mmio {' 'M3 needs a reusable MMIO transport object'
require_text kernel/src/virtqueue.c \
  'if (id >= VIRTQ_NUM)' 'used descriptor IDs must be range checked'
require_order kernel/src/virtio_mmio.c \
  'VIRTIO_CONFIG_S_ACKNOWLEDGE' 'VIRTIO_CONFIG_S_DRIVER' \
  'MMIO status must acknowledge before claiming the driver'
require_text kernel/src/virtio_mmio.c \
  'VIRTIO_MMIO_QUEUE_ALIGN' 'legacy queue registration needs alignment'
require_text kernel/src/virtio_mmio.c \
  'VIRTIO_MMIO_QUEUE_PFN' 'legacy queue registration needs PFN'
require_absent kernel/src/virtio_mmio.c \
  'VIRTIO_MMIO_QUEUE_DESC_LOW' 'M3 must not mix modern queue registers'
require_text kernel/include/timeros/plic.h \
  'void plic_init_hart(void);' 'each scheduling hart must initialize its PLIC context'
require_text kernel/src/timer.c \
  'SIE_SEIE' 'scheduling harts must enable supervisor external interrupts'
require_text kernel/src/trap.c \
  'u32 irq = plic_claim();' 'external interrupts must claim their PLIC source'
require_text kernel/src/trap.c \
  'if (irq == 0)' 'a concurrent empty PLIC claim must be harmless'
require_text kernel/src/trap.c \
  'plic_complete(irq);' 'external interrupts must complete their PLIC source'
require_text kernel/src/trap.c \
  'int handled = 1;' 'claimed PLIC interrupts need a completion result path'
require_text kernel/src/trap.c \
  'return handled;' 'unknown PLIC interrupts must complete before failing'
require_text kernel/src/address.c \
  'PLIC_SIZE' 'the kernel page table must map PLIC MMIO'
require_text kernel/src/virtio_disk.c \
  'task_sleep(' 'block requests must sleep for completion'
require_absent kernel/src/virtio_disk.c \
  'while (_b->disk == 1)' 'block requests must not poll completion'
require_text kernel/src/virtio_disk.c \
  'struct wait_queue descriptors;' 'descriptor exhaustion needs a wait channel'
require_text kernel/src/virtio_disk.c \
  'struct wait_queue completion;' 'each request needs a completion channel'
require_text kernel/src/virtio_disk.c \
  'task_wake(&waiter->completion, 1);' 'IRQ completion must wake its requester'
require_absent kernel/src/main.c \
  'virtio_disk_smoke_test();' 'boot must not perform sleeping block I/O'
require_order kernel/src/task.c \
  'spin_unlock(&task_lock);' 'virtio_disk_smoke_test();' \
  'the init task must drop task_lock before block I/O'
require_text kernel/src/task.c \
  'if (current_proc()->pid == 0)' 'only init may run the pre-user block smoke'
require_text kernel/src/bio.c \
  'struct sleeplock lock;' 'buffer-cache metadata needs a sleeping lock'
require_text kernel/include/timeros/bio.h \
  'struct sleeplock data_lock;' 'each buffer needs a sleeping data lock'
require_text kernel/src/bio.c \
  'sleeplock_init(&bcache.lock);' 'buffer-cache metadata lock must be initialized'
require_text kernel/src/bio.c \
  'sleeplock_init(&b->data_lock);' 'each buffer data lock must be initialized'
require_order kernel/src/main.c \
  'binit();' 'virtio_disk_init();' \
  'the buffer cache must initialize before the block device'
require_text scripts/prepare-fatfs.sh \
  'out/deps/fatfs' 'FatFs must extract only into ignored build output'
require_text kernel/src/fatfs_test.c \
  'printk("QS:FATFS_OK\n");' 'FatFs needs a stable runtime marker'
require_text scripts/m3-smoke.sh \
  'QS:TEST_PASS:m3-smoke' 'M3 needs a stable pass marker'

if [ "$status" -ne 0 ]; then
  exit "$status"
fi

echo "PASS: M3 source contracts"
