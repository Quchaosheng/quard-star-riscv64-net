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
require_text kernel/include/timeros/virtio_net.h \
  '#define VIRTIO_NET_F_MAC 5' 'M4 must require the device MAC feature'
require_text kernel/include/timeros/virtio_net.h \
  '#define VIRTIO_NET_RX_QUEUE 0' 'M4 RX must use queue 0'
require_text kernel/include/timeros/virtio_net.h \
  '#define VIRTIO_NET_TX_QUEUE 1' 'M4 TX must use queue 1'
require_text kernel/include/timeros/virtio_net.h \
  '#define VIRTIO_NET_HDR_SIZE 10' 'M4 must use the legacy ten-byte net header'
require_absent kernel/include/timeros/virtio_net.h \
  'num_buffers' 'M4 must reject mergeable receive buffers'
require_text kernel/src/virtio_net.c \
  'VIRTIO_NET_REJECTED_FEATURES' 'M4 must clear unsupported offloads and controls'
require_text kernel/src/virtio_net.c \
  'VIRTIO_NET_REQUIRED_FEATURES' 'M4 must require MAC negotiation'
require_text kernel/src/virtio_net.c \
  'virtio_mmio_setup_queue(&net.mmio, VIRTIO_NET_RX_QUEUE' \
  'M4 must register the RX queue'
require_text kernel/src/virtio_net.c \
  'virtio_mmio_setup_queue(&net.mmio, VIRTIO_NET_TX_QUEUE' \
  'M4 must register the TX queue'
require_text kernel/src/virtio_net.c \
  '_Static_assert(sizeof(struct virtio_net_hdr) == VIRTIO_NET_HDR_SIZE' \
  'the DMA header layout must be exactly ten bytes'
require_text kernel/src/virtio_net.c \
  'static const u8 expected_mac[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };' \
  'M4 must verify the configured guest MAC'
require_text kernel/src/virtio_net.c \
  'for (int i = 0; i < VIRTQ_NUM; i++)' \
  'M4 must prepost every fixed RX slot'
require_order kernel/src/virtio_net.c \
  'net_post_rx_locked' 'virtio_mmio_driver_ok' \
  'M4 must publish RX buffers before DRIVER_OK'
require_text kernel/src/virtio_net.c \
  'virtq_pop_used_len(&net.rx_queue' \
  'the net ISR must drain RX completions'
require_text kernel/src/virtio_net.c \
  'virtq_pop_used(&net.tx_queue' \
  'the net ISR must drain TX completions'
require_text kernel/src/virtio_net.c \
  'net_completion_push(&net.rx_completions' \
  'RX completion publication must remain bounded'
require_text kernel/src/virtio_net.c \
  'task_wake(&net.rx_wait' 'RX completion must wake receivers'
require_text kernel/src/virtio_net.c \
  'task_wake(&net.tx_wait' 'TX reclaim must wake senders'
require_text kernel/src/virtio_net.c \
  'length < ETHERNET_MIN_FRAME || length > ETHERNET_MAX_FRAME' \
  'TX must reject invalid Ethernet frame lengths'
require_text kernel/include/timeros/virtio_net.h \
  '#define ETHERNET_HEADER_SIZE 14' \
  'RX must allow TAP frames whose wire padding was removed'
require_text kernel/src/virtio_net.c \
  'used_length < VIRTIO_NET_HDR_SIZE + ETHERNET_HEADER_SIZE' \
  'RX ownership checks must use the Ethernet header safety bound'
require_text kernel/src/virtio_net.c \
  'virtq_pop_used_len(&net.rx_queue' \
  'RX used ID and length must be consumed after one acquire'
require_text kernel/src/virtio_net.c \
  'net_validate_header_locked' \
  'RX must reject unsupported legacy net header metadata'
require_text kernel/src/virtio_net.c \
  'net_drop_rx_locked(slot)' \
  'malformed RX frames must be dropped and reposted'
require_text kernel/src/virtio_net.c \
  'while (net.active && generation == net.stats.resets &&' \
  'RX waits must abort across a reset generation'
require_text kernel/src/virtio_net.c \
  'while (net.active && generation == net.stats.resets && net.pending_tx != 0)' \
  'TX waits must abort across a reset generation'
require_text kernel/src/virtio_net.c \
  '(2 * resets_done + 1) * QS_NET_ITERATIONS' \
  'M4 resets must leave traffic on both sides of each reset'
require_text kernel/src/virtio_mmio.c \
  'while (*mmio_reg(dev, VIRTIO_MMIO_STATUS) != 0)' \
  'VirtIO reset must wait for device acknowledgement'
require_text kernel/src/virtio_net.c \
  'capacity < frame_length' \
  'RX must reject a caller buffer that is too small'
require_text kernel/include/timeros/virtio_net.h \
  'int virtio_net_free_tx_slots(void);' \
  'M4 tests need the TX slot baseline accessor'
require_text kernel/include/timeros/virtio_net.h \
  'int virtio_net_posted_rx_buffers(void);' \
  'M4 tests need the RX buffer baseline accessor'
require_text kernel/include/timeros/virtio_net.h \
  'int virtio_net_reset(void);' 'M4 needs a public serialized reset'
require_text kernel/include/timeros/virtio_net.h \
  'struct virtio_net_stats {' 'M4 needs a stable statistics snapshot'
for field in rx_packets tx_packets rx_dropped tx_errors interrupts resets; do
  require_text kernel/include/timeros/virtio_net.h \
    "u64 $field;" "M4 statistics must expose $field"
done
require_text kernel/include/timeros/virtio_net.h \
  'void virtio_net_get_stats(struct virtio_net_stats *stats);' \
  'M4 statistics must be copied under the driver lock'
require_text kernel/include/timeros/virtio_net.h \
  'int virtio_net_free_tx_descriptors(void);' \
  'M4 reset checks need the TX descriptor baseline'
require_text kernel/include/timeros/virtio_net.h \
  'int virtio_net_pending_tx(void);' \
  'M4 reset checks need the pending TX count'
require_text kernel/include/timeros/virtio_net.h \
  'int virtio_net_rx_completions(void);' \
  'M4 reset checks need the RX completion count'
require_text kernel/src/virtio_net.c \
  'int resetting;' 'the net driver needs an explicit resetting state'
require_text kernel/src/virtio_net.c \
  'net_completion_init(&net.rx_completions);' \
  'reset must discard stale RX completions'
require_text kernel/src/virtio_net.c \
  'QS:NET_LINK_OK' 'M4 must report successful link initialization'
require_text kernel/src/virtio_net.c \
  'QS:NET_TX_OK' 'M4 must report successful raw TX'
require_text kernel/src/virtio_net.c \
  'QS:NET_RX_OK' 'M4 must report successful raw RX validation'
require_text kernel/src/virtio_net.c \
  'QS:NET_RESET_OK' 'M4 must report successful recovery'
require_text kernel/src/virtio_net.c \
  'QS:NET_RESETS:%d' 'M4 must report the reset count'
require_text kernel/src/virtio_net.c \
  'QS:NET_STRESS_FRAMES:%d' 'M4 must report the raw frame count'
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

net_source = read("kernel/src/virtio_net.c")
match = re.search(r"void\s+virtio_net_intr\s*\(void\)\s*\{(.*?)^\}",
                  net_source, re.MULTILINE | re.DOTALL)
assert match, "missing virtio_net_intr"
isr = match.group(1)
for forbidden in ("virtio_net_receive(", "memcpy(", "schedule(",
                  "mblock_alloc(", "malloc("):
    assert forbidden not in isr, f"net ISR must not call {forbidden}"

reset = re.search(r"int\s+virtio_net_reset\s*\(void\)\s*\{(.*?)^\}",
                  net_source, re.MULTILINE | re.DOTALL)
assert reset, "missing virtio_net_reset"
body = reset.group(1)
assert "task_wake(&net.rx_wait" in body and "task_wake(&net.tx_wait" in body
assert "virtio_mmio_reset(&net.mmio)" in body
assert body.index("task_wake(&net.rx_wait") < body.index("virtio_mmio_reset")
assert body.index("task_wake(&net.tx_wait") < body.index("virtio_mmio_reset")
assert "net_start_locked()" in body, "reset must rebuild the device"
start = re.search(r"static\s+int\s+net_start_locked\s*\(void\)\s*\{(.*?)^\}",
                  net_source, re.MULTILINE | re.DOTALL)
assert start and "net_post_rx_locked(" in start.group(1), \
       "reset startup must repost RX buffers"
PY
then
  echo "FAIL: M4 board resources must match across all consumers" >&2
  status=1
fi

if [ "$status" -ne 0 ]; then
  exit "$status"
fi

echo "PASS: M4 source contracts"
