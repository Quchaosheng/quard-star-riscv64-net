# M4 VirtIO Net and TAP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete M4 with a second legacy VirtIO MMIO transport, an interrupt-driven copying VirtIO-net driver, and deterministic bidirectional raw Ethernet acceptance through TAP.

**Architecture:** Keep the M3 legacy MMIO and split-ring layers, give RX and TX independent eight-entry queues, and isolate ISR bookkeeping from task-context frame copies with a bounded completion ring. Verify the driver with a test-only raw Ethernet protocol and an `AF_PACKET` TAP peer; defer ARP, IP, Ping, and the migrated TCP/IP stack to M5.

**Tech Stack:** C, RISC-V GCC, VirtIO MMIO v1, split virtqueues, QEMU 8.0.2, SiFive PLIC, DTS, Bash, Python 3 standard library, Linux TAP/AF_PACKET, GCC sanitizers.

---

## File Map

- `platform/quard-star/include/layout.h`: authoritative VirtIO0/1 MMIO and IRQ constants checked against all consumers.
- `patches/qemu/0001-add-quard-star-machine.patch`: second `virtio-mmio` sysbus transport and bus ordering.
- `platform/quard-star/dts/quard_star_kernel.dts`: block and net MMIO nodes with IRQ 1 and IRQ 2.
- `kernel/include/timeros/virtio_mmio.h`, `kernel/src/virtio_mmio.c`: negotiated-feature output and byte-oriented config reads.
- `kernel/include/timeros/virtio_net_completion.h`, `kernel/src/virtio_net_completion.c`: bounded, allocation-free completion ring shared by ISR and task context.
- `kernel/include/timeros/virtio_net.h`, `kernel/src/virtio_net.c`: MAC negotiation, RX/TX queues, fixed buffers, interrupt completion, counters, reset, and copying APIs.
- `kernel/src/plic.c`, `kernel/src/trap.c`, `kernel/src/address.c`: IRQ 2 enable/dispatch and second MMIO mapping.
- `kernel/include/timeros/selftest.h`, `kernel/src/selftest.c`, `kernel/src/main.c`, `kernel/src/task.c`: M4 coordination and scheduled raw-frame guest test.
- `scripts/tap-up.sh`, `scripts/tap-down.sh`: idempotent TAP lifecycle without route/NAT/firewall changes.
- `scripts/m4-peer.py`: deterministic EtherType peer over `AF_PACKET`.
- `scripts/m4-build.sh`, `scripts/m4-smoke.sh`, `scripts/m4-stress.sh`: M4 build and acceptance profiles.
- `tests/host/test_m4_contracts.sh`, `tests/host/test_m4_completion.c`, `tests/host/test_m4_completion.sh`, `tests/host/test_m4_tap_scripts.sh`, `tests/host/test_m4_smoke_script.sh`: source, state, shell, and fake-QEMU coverage.

### Task 1: Lock the M4 Contracts

**Files:**
- Create: `tests/host/test_m4_contracts.sh`
- Create: `tests/host/test_m4_smoke_script.sh`
- Modify: `Makefile`

- [ ] **Step 1: Write the failing source-contract test**

Copy the `require_text`, `require_absent`, and `require_order` helpers from
`tests/host/test_m3_contracts.sh`. Add these initial requirements:

```sh
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
```

- [ ] **Step 2: Write the failing fake-QEMU acceptance test**

Follow `tests/host/test_m3_smoke_script.sh`. Create temporary M4 artifacts and
a fake QEMU whose complete serial log contains all M3 markers plus:

```text
QS:NET_LINK_OK
QS:NET_IRQ_OK
QS:NET_TX_OK
QS:NET_RX_OK
QS:NET_RESET_OK
QS:NET_RESETS:1
QS:NET_STRESS_FRAMES:32
QS:TEST_PASS:m4-smoke
```

Invoke `scripts/m4-smoke.sh` with `QS_TAP_MANAGED=0`, a fake peer command, and
the fake QEMU. Require complete markers plus exit 0 to pass. Require failure
when `QS:NET_RX_OK` is removed, when `QS:NET_STRESS_FRAMES:320` replaces the
exact count, when a `QS:TEST_FAIL` marker appears, or when QEMU exits 7.

- [ ] **Step 3: Register both tests and verify RED**

Add both scripts to `make test-host`, run:

```bash
tests/host/test_m4_contracts.sh
tests/host/test_m4_smoke_script.sh
```

Expected: both fail only on missing M4 production files and scripts; all M3
host tests remain green.

### Task 2: Add the Second Board Transport and IRQ (M4A)

**Files:**
- Create: `platform/quard-star/include/layout.h`
- Modify: `patches/qemu/0001-add-quard-star-machine.patch`
- Modify: `platform/quard-star/dts/quard_star_kernel.dts`
- Modify: `kernel/include/timeros/virtio.h`
- Modify: `kernel/include/timeros/plic.h`
- Modify: `kernel/src/address.c`
- Modify: `kernel/src/plic.c`
- Modify: `kernel/src/trap.c`
- Modify: `tests/host/test_m1_dts.sh`
- Modify: `tests/host/test_m4_contracts.sh`

- [ ] **Step 1: Add failing resource-equality checks**

Extend `test_m4_contracts.sh` with a small Python block that parses hexadecimal
constants and asserts these exact tuples in the board header, QEMU patch, DTS,
and kernel headers:

```text
block = (0x10100000, 0x1000, 1)
net   = (0x10101000, 0x1000, 2)
```

Also extend `test_m1_dts.sh` to compile the base and M2 kernel DTS files and
require two `compatible = "virtio,mmio"` nodes. Run both tests and confirm RED
because the common header, second QEMU device, and DTS nodes are absent.

- [ ] **Step 2: Add the narrow board resource header**

Create `platform/quard-star/include/layout.h`:

```c
#ifndef QS_PLATFORM_LAYOUT_H
#define QS_PLATFORM_LAYOUT_H

#define QS_VIRTIO_BLOCK_BASE 0x10100000ULL
#define QS_VIRTIO_NET_BASE   0x10101000ULL
#define QS_VIRTIO_MMIO_SIZE  0x1000ULL
#define QS_VIRTIO_BLOCK_IRQ  1
#define QS_VIRTIO_NET_IRQ    2

#endif
```

Add `-I../platform/quard-star/include` to the kernel include path and replace
the block-only base constant with `QS_VIRTIO_BLOCK_BASE`. Do not move unrelated
board constants in this checkpoint.

- [ ] **Step 3: Add QEMU transport 1**

Update the QEMU patch's `quard_star.h` section with `QUARD_STAR_VIRTIO1` and
`QUARD_STAR_VIRTIO1_IRQ = 2`. Add this memmap entry after transport 0:

```c
[QUARD_STAR_VIRTIO1] = { 0x10101000, 0x1000 },
```

Change `quard_star_virtio_mmio_create` to create transport 0 first and
transport 1 second, each wired to its own PLIC source. Preserve bus numbering
so block remains `virtio-mmio-bus.0` and net becomes `virtio-mmio-bus.1`.

- [ ] **Step 4: Add both DTS nodes**

Under `/soc` in `quard_star_kernel.dts`, add:

```dts
virtio_block: virtio_mmio@10100000 {
    compatible = "virtio,mmio";
    reg = <0 0x10100000 0 0x1000>;
    interrupt-parent = <&plic>;
    interrupts = <1>;
};

virtio_net: virtio_mmio@10101000 {
    compatible = "virtio,mmio";
    reg = <0 0x10101000 0 0x1000>;
    interrupt-parent = <&plic>;
    interrupts = <2>;
};
```

The M2 overlay inherits both nodes; do not duplicate them there.

- [ ] **Step 5: Map and enable transport 1**

Map `QS_VIRTIO_NET_BASE` for one page in `kvmmake`. Define:

```c
#define PLIC_VIRTIO0_IRQ QS_VIRTIO_BLOCK_IRQ
#define PLIC_VIRTIO1_IRQ QS_VIRTIO_NET_IRQ
```

In `plic_init_hart`, set priorities for both sources and write an enable mask
containing both bits. In `handle_interrupt`, dispatch IRQ 2 to
`virtio_net_intr` only when `QS_M4_TEST` is enabled; otherwise complete it as
unexpected. Keep the existing zero-claim and unified completion behavior.

- [ ] **Step 6: Verify QEMU, DTS, and earlier regressions**

Run:

```bash
tests/host/test_m1_dts.sh
tests/host/test_m4_contracts.sh
make m1-build && make m1-smoke
make m3-build && make m3-smoke
```

Expected: resource/DTS checks pass; M4 contract remains RED only on the driver
and scripts; M1 and M3 still pass with no net device attached.

- [ ] **Step 7: Commit and push M4A**

```bash
git add platform patches kernel tests/host
git commit -m "feat: add second quard star virtio transport"
git push origin codex/m4-virtio-net
```

### Task 3: Build the Bounded Completion Ring

**Files:**
- Create: `kernel/include/timeros/virtio_net_completion.h`
- Create: `kernel/src/virtio_net_completion.c`
- Create: `tests/host/test_m4_completion.c`
- Create: `tests/host/test_m4_completion.sh`
- Modify: `kernel/Makefile`
- Modify: `Makefile`

- [ ] **Step 1: Write the failing sanitizer-backed state test**

Define the desired API in `test_m4_completion.c`:

```c
struct net_completion_ring ring;
u16 slot;

net_completion_init(&ring);
for (u16 i = 0; i < NET_COMPLETION_CAPACITY; i++)
    assert(net_completion_push(&ring, i) == 0);
assert(net_completion_push(&ring, 99) == -1);
for (u16 i = 0; i < NET_COMPLETION_CAPACITY; i++) {
    assert(net_completion_pop(&ring, &slot) == 1);
    assert(slot == i);
}
assert(net_completion_pop(&ring, &slot) == 0);

for (int cycle = 0; cycle < 3; cycle++) {
    assert(net_completion_push(&ring, 3) == 0);
    assert(net_completion_pop(&ring, &slot) == 1);
}
net_completion_reset(&ring);
assert(net_completion_count(&ring) == 0);
```

Compile with `-std=c11 -Wall -Wextra -Werror
-fsanitize=address,undefined`, run it, and confirm compilation fails because
the completion module does not exist.

- [ ] **Step 2: Implement the minimal fixed ring**

Define:

```c
#define NET_COMPLETION_CAPACITY VIRTQ_NUM

struct net_completion_ring {
    u16 slots[NET_COMPLETION_CAPACITY];
    u16 read_index;
    u16 write_index;
    u16 count;
};

void net_completion_init(struct net_completion_ring *ring);
void net_completion_reset(struct net_completion_ring *ring);
int net_completion_push(struct net_completion_ring *ring, u16 slot);
int net_completion_pop(struct net_completion_ring *ring, u16 *slot);
int net_completion_count(const struct net_completion_ring *ring);
```

Reject null pointers and full pushes without changing indexes. Wrap indexes by
`NET_COMPLETION_CAPACITY`; do not allocate memory or use kernel locks inside
this pure state module.

- [ ] **Step 3: Register and verify GREEN**

Add the source to `kernel/Makefile`, register the test under `make test-host`,
and run:

```bash
tests/host/test_m4_completion.sh
make test-host
```

Expected: completion and all earlier host tests pass.

### Task 4: Negotiate VirtIO-net and Post RX Buffers

**Files:**
- Create: `kernel/include/timeros/virtio_net.h`
- Create: `kernel/src/virtio_net.c`
- Modify: `kernel/include/timeros/virtio_mmio.h`
- Modify: `kernel/src/virtio_mmio.c`
- Modify: `kernel/include/timeros/os.h`
- Modify: `kernel/src/main.c`
- Modify: `kernel/Makefile`
- Modify: `tests/host/test_m4_contracts.sh`

- [ ] **Step 1: Add failing negotiation and RX contracts**

Require device ID 1, `VIRTIO_NET_F_MAC`, rejected offload/control features,
queue 0 setup, queue 1 setup, eight preposted RX slots, ten-byte header size,
MAC `52:54:00:12:34:56`, and `DRIVER_OK` after queue publication. Require the
absence of mergeable RX header fields.

- [ ] **Step 2: Extend MMIO feature/config access**

Change the common initializer to report negotiated low features without
adding net knowledge:

```c
int virtio_mmio_init(struct virtio_mmio *dev, u64 base, u32 device_id,
                     u32 rejected_features, u32 required_features,
                     u32 *negotiated_features);
u8 virtio_mmio_config8(struct virtio_mmio *dev, u32 offset);
void virtio_mmio_reset(struct virtio_mmio *dev);
```

Return `-1` when a required bit is absent. Update the block call with required
features 0 and a null negotiated pointer. `config8` reads a volatile byte from
`VIRTIO_MMIO_CONFIG + offset`; reset writes status 0 and performs a full
barrier.

- [ ] **Step 3: Define the driver state and wire format**

In `virtio_net.h`, define Ethernet bounds and a packed ten-byte header:

```c
#define VIRTIO_NET_DEVICE_ID 1
#define VIRTIO_NET_RX_QUEUE 0
#define VIRTIO_NET_TX_QUEUE 1
#define VIRTIO_NET_HDR_SIZE 10
#define ETHERNET_MIN_FRAME 60
#define ETHERNET_MAX_FRAME 1514

struct virtio_net_hdr {
    u8 flags;
    u8 gso_type;
    u16 hdr_len;
    u16 gso_size;
    u16 csum_start;
    u16 csum_offset;
} __attribute__((packed));
```

Define fixed RX slots containing header plus frame storage, two page-aligned
queue regions, two `struct virtqueue` objects, one spinlock, RX/TX wait queues,
completion ring, device state, MAC, ownership arrays, and counters.

- [ ] **Step 4: Initialize and prepost RX**

`virtio_net_init` must:

1. negotiate device ID 1 and require `VIRTIO_NET_F_MAC`;
2. reject CSUM, GUEST_CSUM, TSO4/6, ECN, UFO, MRG_RXBUF, CTRL_VQ,
   CTRL_RX, CTRL_VLAN, GUEST_ANNOUNCE, MQ, and CTRL_MAC_ADDR;
3. read and compare all six MAC bytes;
4. initialize queue 0 and queue 1;
5. allocate one descriptor for each RX slot, set its address/size and
   `VRING_DESC_F_WRITE`, and submit it to RX;
6. initialize waits, completion state, ownership, and counters;
7. set `DRIVER_OK` only after all RX slots are published.

Initialization failure prints `QS:TEST_FAIL:m4-net:init:<code>` and leaves the
device failed. M4 builds treat the net device as required; earlier builds do
not call the initializer.

- [ ] **Step 5: Build and verify focused GREEN**

Call `virtio_net_init` from `os_main` under `QS_M4_TEST`, after block init and
before tasks start. Run:

```bash
tests/host/test_m4_contracts.sh
make m3-build
make m1-build
```

Expected: both earlier builds pass; M4 contract now fails only on RX/TX
completion, reset, and scripts.

### Task 5: Add Interrupt-Driven RX and TX

**Files:**
- Modify: `kernel/include/timeros/virtio_net.h`
- Modify: `kernel/src/virtio_net.c`
- Modify: `kernel/src/trap.c`
- Modify: `tests/host/test_m4_contracts.sh`

- [ ] **Step 1: Add failing ISR, copy, and backpressure contracts**

Require `virtio_mmio_ack_interrupt`, draining of both used rings, range and
ownership validation before slot indexing, bounded `net_completion_push`,
RX/TX `task_wake`, RX/TX `task_sleep`, `ETHERNET_MIN_FRAME` and
`ETHERNET_MAX_FRAME` checks, and descriptor/buffer baseline accessors. Require
that the ISR does not call `virtio_net_receive`, `memcpy`, `schedule`, or any
allocation function.

- [ ] **Step 2: Implement TX submission**

Implement:

```c
int virtio_net_send(const void *frame, u32 length);
```

Reject null, short, or oversized frames. Under the net lock, sleep on TX space
while the device is active and no fixed slot is free. Copy a zeroed ten-byte
header plus frame into the bounce slot, allocate one descriptor, mark slot
ownership by descriptor head, submit to queue 1, notify, and return after
publication. If reset/failure begins while waiting, return `-1`.

- [ ] **Step 3: Implement RX delivery in task context**

Implement:

```c
int virtio_net_receive(void *frame, u32 capacity, u32 *length,
                       u64 deadline);
```

Reject null outputs. Under the net lock, sleep on RX completion until a slot is
available, the deadline expires, or the device fails. Pop one slot, copy only
the validated Ethernet bytes, reset the header, resubmit the same descriptor
to queue 0, notify RX, and return 0. On insufficient caller capacity, drop and
repost the frame, increment `rx_dropped`, and return `-1`.

- [ ] **Step 4: Implement bounded ISR completion**

`virtio_net_intr` acknowledges MMIO, takes the net lock, and drains RX then TX.
For RX, validate the active descriptor and used length before indexing/copying.
Push the slot ID to the fixed completion ring and wake RX; if full, increment
`rx_dropped` and immediately repost. For TX, validate ownership, reclaim the
single descriptor and bounce slot, decrement pending TX, and wake TX-space
waiters. Print `QS:NET_IRQ_OK` only after the first valid net completion.

Any invalid/duplicate used ID or corrupt ownership calls one bounded failure
helper that stops submissions and wakes both wait channels. Do not panic in
the ISR.

- [ ] **Step 5: Verify focused builds and contracts**

Run:

```bash
tests/host/test_m4_completion.sh
tests/host/test_m4_contracts.sh
make m3-build && make m3-smoke
```

Expected: earlier storage acceptance passes; M4 contracts remain RED only on
reset, TAP, and final markers.

### Task 6: Add Reset, Stats, and the Guest Raw-Frame Test (M4B)

**Files:**
- Modify: `kernel/include/timeros/virtio_net.h`
- Modify: `kernel/src/virtio_net.c`
- Modify: `kernel/include/timeros/selftest.h`
- Modify: `kernel/src/selftest.c`
- Modify: `kernel/src/task.c`
- Modify: `tests/host/test_m4_contracts.sh`

- [ ] **Step 1: Add failing reset/stat/self-test contracts**

Require a public `virtio_net_reset`, a stats snapshot containing RX/TX packets,
drops, errors, interrupts, and resets, explicit resetting/failed states,
wakeups before queue reinitialization, stale completion reset, all RX buffers
reposted, and these marker calls:

```text
QS:NET_LINK_OK
QS:NET_TX_OK
QS:NET_RX_OK
QS:NET_RESET_OK
QS:NET_RESETS:%d
QS:NET_STRESS_FRAMES:%d
```

- [ ] **Step 2: Implement serialized reset**

Expose the reset operation as:

```c
int virtio_net_reset(void);
```

Under the device lock, set resetting, reject new TX, set an error result for
waiters, and wake RX/TX queues. Reset transport status, rebuild both queue
objects and ownership arrays in their existing page regions, reset completion
indexes and pending counters, renegotiate features/MAC, register queues,
repost every RX buffer, set `DRIVER_OK`, increment the reset counter, and mark
active. A failed phase leaves the device failed and returns `-1`.

- [ ] **Step 3: Add stats/baseline accessors**

Expose locked snapshots:

```c
struct virtio_net_stats {
    u64 rx_packets;
    u64 tx_packets;
    u64 rx_dropped;
    u64 tx_errors;
    u64 interrupts;
    u64 resets;
};

void virtio_net_get_stats(struct virtio_net_stats *stats);
int virtio_net_free_tx_descriptors(void);
int virtio_net_pending_tx(void);
int virtio_net_rx_completions(void);
```

Return copies, never pointers to mutable driver state.

- [ ] **Step 4: Implement the deterministic raw protocol**

Under `QS_M4_TEST`, build Ethernet frames with destination broadcast for the
request, source guest MAC, EtherType `0x88b5`, a big-endian 32-bit sequence,
a 16-bit payload length, deterministic bytes `sequence ^ offset ^ 0x5a`, and a
32-bit additive checksum. The peer response is addressed to the guest MAC and
uses the same validated sequence/payload.

For `QS_NET_ITERATIONS` iterations, send one request, wait for the matching
response with a finite deadline, and compare every field. Reject stale,
duplicate, malformed, or wrong-sequence responses with a stable failure code.

- [ ] **Step 5: Integrate M4 self-test bits**

Add M4 link, IRQ, TX, RX, reset, and stress bits to `selftest.c`. Under
`QS_M4_TEST`, require every M3 bit plus every M4 bit before printing
`QS:TEST_PASS:m4-smoke` or `m4-stress` and exiting QEMU.

From PID 0's scheduled first-run hook, after M3 FatFs verification, run the M4
raw test. Reset once halfway through quick acceptance, then continue traffic.
At the end require TX free descriptors, pending count, RX completion count,
and fixed slot ownership to match baseline before printing success markers.

- [ ] **Step 6: Verify compilation and commit M4B**

Run host tests and an M4 kernel build with the net device compiled:

```bash
make test-host
QS_STAGE=m4 QS_KERNEL_CPPFLAGS='-DQS_M2C_TEST -DQS_M3_TEST -DQS_M4_TEST -DQS_NET_ITERATIONS=32' QS_KERNEL_FATFS=1 scripts/m1-build.sh
```

Expected: compilation passes; real M4 acceptance is still blocked only on the
TAP peer/scripts.

Commit:

```bash
git add kernel tests/host Makefile
git commit -m "feat: add interrupt driven virtio net driver"
git push origin codex/m4-virtio-net
```

### Task 7: Add Idempotent TAP and Raw Peer Tooling

**Files:**
- Create: `scripts/tap-up.sh`
- Create: `scripts/tap-down.sh`
- Create: `scripts/m4-peer.py`
- Create: `tests/host/test_m4_tap_scripts.sh`
- Modify: `scripts/check-env.sh`
- Modify: `Makefile`

- [ ] **Step 1: Write failing TAP lifecycle tests**

Use fake `ip`, `id`, and `sudo` executables that log argv and model interface
existence. Verify:

- first `tap-up.sh tap-test` adds TAP, assigns `192.168.100.1/24`, and sets up;
- second invocation succeeds without a duplicate add/address failure;
- `tap-down.sh tap-test` removes the interface;
- second down succeeds when absent;
- `QS_TAP_USER` is passed to `ip tuntap add dev tap-test mode tap user <name>`;
- a failing privileged command returns nonzero and names that command.

Run and confirm RED because the scripts do not exist.

- [ ] **Step 2: Implement privilege-aware TAP scripts**

Both scripts use `set -eu`, `QS_TAP_IFACE`/first argument with default `tap0`,
and `QS_TAP_ADDR` default `192.168.100.1/24`. If effective UID is zero, invoke
`ip` directly; otherwise use `${QS_SUDO:-sudo}`. Query link/address state before
mutating it. Do not touch routes, forwarding, NAT, firewall, DNS, or Windows.
Install an error trap that prints the failed operation.

- [ ] **Step 3: Write and unit-test raw protocol helpers**

In `m4-peer.py`, keep pure functions separate from socket I/O. Use this exact
wire encoding; trailing zero padding to the 60-byte Ethernet minimum is not
part of the payload checksum:

```python
import struct

ETHERTYPE = 0x88B5
GUEST_MAC = bytes.fromhex("525400123456")
HOST_MAC = bytes.fromhex("525400123457")
BROADCAST = b"\xff" * 6

def payload_checksum(payload: bytes) -> int:
    return sum(payload) & 0xFFFFFFFF

def decode_request(frame: bytes) -> tuple[int, bytes]:
    if len(frame) < 24:
        raise ValueError("truncated M4 frame")
    destination, source, ethertype = struct.unpack_from("!6s6sH", frame, 0)
    if destination != BROADCAST or source != GUEST_MAC or ethertype != ETHERTYPE:
        raise ValueError("unexpected M4 Ethernet header")
    sequence, length = struct.unpack_from("!IH", frame, 14)
    end = 20 + length
    if end + 4 > len(frame):
        raise ValueError("truncated M4 payload")
    payload = frame[20:end]
    checksum = struct.unpack_from("!I", frame, end)[0]
    if checksum != payload_checksum(payload):
        raise ValueError("bad M4 checksum")
    return sequence, payload

def encode_response(sequence: int, payload: bytes) -> bytes:
    body = struct.pack("!IH", sequence, len(payload)) + payload
    body += struct.pack("!I", payload_checksum(payload))
    frame = GUEST_MAC + HOST_MAC + struct.pack("!H", ETHERTYPE) + body
    return frame.ljust(60, b"\0")
```

Add Python assertions in `test_m4_tap_scripts.sh` for valid encode/decode and
for rejecting truncated Ethernet headers, wrong EtherType/MAC, inconsistent
payload length, and bad checksum before opening a raw socket.

- [ ] **Step 4: Implement the AF_PACKET peer loop**

Bind `socket(AF_PACKET, SOCK_RAW, htons(0x88B5))` to the chosen interface.
For each valid request, record the strictly increasing sequence, generate the
response, and send it through the same socket. Accept `--count`, `--timeout`,
`--ready-file`, and `--stats-file`. Exit nonzero on timeout, malformed M4
traffic, duplicate/out-of-order sequence, count mismatch, or socket error.

- [ ] **Step 5: Register and verify GREEN**

Add `ip` and `python3` environment checks if not already present, register the
test, and run:

```bash
tests/host/test_m4_tap_scripts.sh
make test-host
```

Expected: all unprivileged host tests pass without creating a real TAP.

### Task 8: Add M4 Build, Smoke, and Stress Profiles

**Files:**
- Create: `scripts/m4-build.sh`
- Create: `scripts/m4-smoke.sh`
- Create: `scripts/m4-stress.sh`
- Modify: `scripts/m2c-smoke.sh`
- Modify: `tests/host/test_m4_smoke_script.sh`
- Modify: `Makefile`

- [ ] **Step 1: Generalize QEMU net arguments with a failing fake test**

Extend the fake-QEMU test to record argv and require these exact arguments when
`QS_TAP_IFACE=tap-test`:

```text
-global virtio-mmio.force-legacy=true
-netdev tap,id=net0,ifname=tap-test,script=no,downscript=no
-device virtio-net-device,netdev=net0,mac=52:54:00:12:34:56,bus=virtio-mmio-bus.1
```

Confirm RED because `m2c-smoke.sh` does not add them.

- [ ] **Step 2: Add optional net arguments without changing old defaults**

Build QEMU argv with `set --`. Append the three net arguments only when
`QS_TAP_IFACE` is nonempty. Keep the existing block device on bus 0. Re-run all
M1/M2/M3 fake-QEMU tests and require identical default behavior.

- [ ] **Step 3: Add the M4 quick build**

`m4-build.sh` runs FatFs preparation, uses M2 DTS files, enables FATFS, sets
stage `m4`, and passes:

```text
-DQS_M2C_TEST -DQS_M3_TEST -DQS_M4_TEST
-DQS_ALLOC_ITERATIONS=10000 -DQS_MIGRATION_TARGET=100
-DQS_FATFS_ITERATIONS=4 -DQS_NET_ITERATIONS=32
-DQS_NET_RESETS=1
```

Then reuse `m1-build.sh`.

- [ ] **Step 4: Add the quick TAP acceptance wrapper**

`m4-smoke.sh` must:

1. create TAP unless `QS_TAP_MANAGED=0`;
2. start `m4-peer.py --count 32` and wait for its ready file;
3. export the TAP interface, stage `m4`, test name `m4-smoke`, and exact M3/M4
   marker set to `m2c-smoke.sh`;
4. trap EXIT/INT/TERM to stop QEMU/peer and remove only a TAP it created;
5. require both QEMU and peer exit status 0 and peer stats count 32.

Do not report success from serial markers alone.

- [ ] **Step 5: Add the M4 stress profile**

`m4-stress.sh` uses isolated stage `m4-stress`, `QS_NET_ITERATIONS=1000`,
`QS_NET_RESETS=10`, the M3 allocator/migration stress values, and
`QS_STRESS_MIN_TICKS=1200000000ULL`. Require exact
`QS:NET_STRESS_FRAMES:1000`, `QS:NET_RESETS:10`, all M3/M4 markers, peer
count 1000, no failure marker, minimum ticks, and zero descriptor/buffer leak.

- [ ] **Step 6: Register targets and verify fake GREEN**

Add `m4-build`, `m4-smoke`, and `m4-stress` Make targets. Run:

```bash
tests/host/test_m4_smoke_script.sh
make test-host
```

Expected: complete fake logs/argv pass; missing, malformed, nonzero, and peer
failure cases fail; all M1-M3 script tests remain green.

### Task 9: Verify and Publish M4 (M4C)

**Files:**
- Modify only files needed for defects reproduced by the verification below.

- [ ] **Step 1: Run host and environment checks**

```bash
make check-env check-sources test-host
tests/host/test_m1_build_contracts.sh
git diff --check
```

Expected: every host test passes with no whitespace errors.

- [ ] **Step 2: Run all earlier-stage regressions**

```bash
make m1-build && make m1-smoke
make m2a-build && make m2a-smoke
make m2b-build && make m2b-smoke
make m2c-build && make m2c-smoke
make m3-build && make m3-smoke
```

Expected: every earlier build/smoke exits 0 without a net device attached.

- [ ] **Step 3: Run real M4 quick TAP acceptance**

```bash
make m4-build
make m4-smoke
```

Expected: 32 request/response frames, one reset, all exact M4 markers, peer
count 32, QEMU/peer exit 0, and TAP cleanup. `tcpdump -ni tap0 -e` may be used
for diagnosis but is not the pass oracle.

- [ ] **Step 4: Run real 120-second M4 stress**

```bash
make m4-stress
```

Expected: 100000 allocator operations, 10000 migrations, 128 FatFs cycles,
1000 bidirectional Ethernet frames, ten net resets, at least 1200000000 ticks,
and all descriptor/buffer/request baselines restored.

- [ ] **Step 5: Review, commit, and publish M4C**

Request an independent code review over the M4A base through the working tree.
Fix every Critical/Important finding with a failing test first, then repeat the
affected quick/stress acceptance.

```bash
git diff --check
git status --short --branch
git add Makefile kernel platform patches scripts tests/host
git commit -m "feat: verify virtio net over tap"
git push origin codex/m4-virtio-net
git fetch origin codex/m4-virtio-net
test "$(git rev-parse HEAD)" = \
     "$(git rev-parse origin/codex/m4-virtio-net)"
git status --short --branch
```

Expected: clean worktree and identical local/remote M4 branch heads.
