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
  if [ -z "$first_line" ] || [ -z "$second_line" ] || \
     [ "$first_line" -ge "$second_line" ]; then
    echo "FAIL: $message" >&2
    status=1
  fi
}

require_text platform/quard-star/include/layout.h \
  '#define QS_VIRTIO_NET_BASE 0x10101000ULL' \
  'M4 needs the fixed net MMIO address'
require_text platform/quard-star/include/layout.h \
  '#define QS_VIRTIO_NET_IRQ 2' 'M4 needs the fixed net PLIC source'
require_text patches/qemu/0001-add-quard-star-machine.patch \
  'QUARD_STAR_VIRTIO1' 'QEMU needs the second MMIO transport'
require_text platform/quard-star/dts/quard_star_kernel.dts \
  'virtio_mmio@10101000' 'kernel DTS needs the net transport'
require_text kernel/src/plic.c \
  '1U << PLIC_VIRTIO1_IRQ' 'every scheduling hart must enable net IRQ 2'
require_text kernel/src/trap.c \
  'virtio_net_intr();' 'external IRQ 2 must dispatch to the net driver'
require_text kernel/include/timeros/virtio_net.h \
  'int virtio_net_send(const void *frame, u32 length);' \
  'M4 needs the copying TX API'
require_text kernel/include/timeros/virtio_net.h \
  'int virtio_net_receive(void *frame, u32 capacity, u32 *length,' \
  'M4 needs the copying RX API'
require_text kernel/src/virtio_net.c \
  'task_sleep(' 'RX and TX backpressure must sleep'
require_absent kernel/src/virtio_net.c \
  'mblock_alloc' 'the M4 ISR path must not enter the future stack allocator'
require_text scripts/m4-smoke.sh \
  'QS:TEST_PASS:m4-smoke' 'M4 needs a stable quick pass marker'

if ! python3 - "$root" <<'PY'
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])

def read(path):
    try:
        return (root / path).read_text(encoding="utf-8")
    except FileNotFoundError:
        raise AssertionError(f"missing {path}")

layout = read("platform/quard-star/include/layout.h")
qemu = read("patches/qemu/0001-add-quard-star-machine.patch")
dts = read("platform/quard-star/dts/quard_star_kernel.dts")
virtio = read("kernel/include/timeros/virtio.h")
plic = read("kernel/include/timeros/plic.h")

def macro(text, name):
    match = re.search(rf"^#define\s+{name}\s+(0x[0-9a-fA-F]+|[0-9]+)",
                      text, re.MULTILINE)
    assert match, f"missing macro {name}"
    return int(match.group(1), 0)

assert (macro(layout, "QS_VIRTIO_BLOCK_BASE"),
        macro(layout, "QS_VIRTIO_MMIO_SIZE"),
        macro(layout, "QS_VIRTIO_BLOCK_IRQ")) == (0x10100000, 0x1000, 1)
assert (macro(layout, "QS_VIRTIO_NET_BASE"),
        macro(layout, "QS_VIRTIO_MMIO_SIZE"),
        macro(layout, "QS_VIRTIO_NET_IRQ")) == (0x10101000, 0x1000, 2)

for token in (
    "[QUARD_STAR_VIRTIO0]     = { 0x10100000,        0x1000 }",
    "[QUARD_STAR_VIRTIO1]     = { 0x10101000,        0x1000 }",
    "QUARD_STAR_VIRTIO0_IRQ = 1",
    "QUARD_STAR_VIRTIO1_IRQ = 2",
):
    assert token in qemu, f"QEMU resource mismatch: {token}"

for token in (
    "virtio_mmio@10100000",
    "reg = <0 0x10100000 0 0x1000>",
    "interrupts = <1>",
    "virtio_mmio@10101000",
    "reg = <0 0x10101000 0 0x1000>",
    "interrupts = <2>",
):
    assert token in dts, f"DTS resource mismatch: {token}"

assert "#define VIRTIO0 QS_VIRTIO_BLOCK_BASE" in virtio
assert "#define VIRTIO1 QS_VIRTIO_NET_BASE" in virtio
assert "#define PLIC_VIRTIO0_IRQ QS_VIRTIO_BLOCK_IRQ" in plic
assert "#define PLIC_VIRTIO1_IRQ QS_VIRTIO_NET_IRQ" in plic
PY
then
  echo "FAIL: M4 board resources must match across all consumers" >&2
  status=1
fi

if [ "$status" -ne 0 ]; then
  exit "$status"
fi

echo "PASS: M4 source contracts"
