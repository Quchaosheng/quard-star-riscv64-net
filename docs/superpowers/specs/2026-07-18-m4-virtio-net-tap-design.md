# M4 VirtIO Net and TAP Design

## Goal

Add the second legacy VirtIO MMIO transport and an interrupt-driven
VirtIO-net driver, then verify bidirectional raw Ethernet traffic through a
WSL2 TAP interface. Keep the M1-M3 storage, SMP, and FatFs paths unchanged.
Do not move ARP, IPv4, ICMP, sockets, or the tiny TCP/IP stack into M4; those
remain M5 and later work.

## Delivery Split

M4 is delivered on `codex/m4-virtio-net` in three checkpoints:

- M4A adds the board resource contract, second QEMU MMIO transport, kernel
  DTS nodes, address mapping, IRQ 2 routing, and device discovery smoke.
- M4B adds the legacy VirtIO-net RX/TX driver, fixed buffer ownership,
  interrupt completion, counters, reset, and host-side state tests.
- M4C adds idempotent TAP lifecycle scripts, a raw Ethernet peer, quick and
  stress acceptance profiles, and full earlier-stage regression.

Each checkpoint must pass its focused host and QEMU tests before the next
checkpoint starts.

## Scope Boundary

M4 carries Ethernet frames only. The guest test uses a project-specific
experimental EtherType and a deterministic payload containing a sequence
number and checksum. The TAP peer validates each guest frame and returns a
corresponding frame to the guest MAC.

This raw-frame protocol is test-only and does not become a public network
API. M5 will connect the driver to the migrated `pktbuf` and netif layers and
will own ARP, IPv4, ICMP, addressing, and Ping. The unavailable
`Quchaosheng/tiny-tcpip-stack` repository does not block M4 because no
protocol-stack source is required here.

## Board and Transport Contract

The fixed resources are:

| Device | MMIO base | PLIC IRQ | QEMU bus |
|---|---:|---:|---|
| VirtIO block | `0x10100000` | 1 | `virtio-mmio-bus.0` |
| VirtIO net | `0x10101000` | 2 | `virtio-mmio-bus.1` |

The guest MAC is `52:54:00:12:34:56`. M4 introduces the missing board-level
VirtIO address/IRQ constants and host contract tests compare the QEMU patch,
DTS, and kernel definitions against them. It does not expand this work into a
general refactor of unrelated UART, RTC, DRAM, or trusted-domain constants.

The QEMU machine creates two `virtio-mmio` sysbus devices in ascending address
order. M4 build and smoke commands attach block to bus 0 and net to bus 1 and
force the legacy transport. Both the single-hart base kernel DTS and the
dual-hart overlay expose the two MMIO nodes with the correct address, size,
interrupt parent, and IRQ.

The kernel maps the second 4 KiB MMIO region. Each scheduling hart enables
PLIC sources 1 and 2. The common external interrupt handler claims once,
dispatches IRQ 1 to block and IRQ 2 to net, completes every nonzero claim, and
treats a concurrent zero claim as harmless.

## Supported VirtIO-net Profile

M4 keeps legacy VirtIO MMIO v1 and the existing split-ring implementation.
The net device uses:

- device ID 1;
- queue 0 for receive;
- queue 1 for transmit;
- one eight-entry split ring per queue;
- `VIRTIO_NET_F_MAC` so the driver can read and verify the configured MAC;
- a ten-byte legacy `virtio_net_hdr` with no offload metadata.

The driver rejects checksum offload, host/guest TSO, ECN, UFO, mergeable RX
buffers, control queues, guest announce, multiqueue, and other layouts that
would change header or buffer ownership. Unsupported features are cleared
before `FEATURES_OK`. Initialization stops on identity, feature, MAC, or queue
setup failure and never publishes descriptors after a failed status sequence.

## Module Boundaries

`virtio_mmio` remains transport-only. M4 extends it only where two queues or
byte-oriented configuration access require a reusable operation; it does not
gain Ethernet knowledge.

`virtqueue` continues to own descriptor allocation, avail publication, used
consumption, and chain reclamation. RX and TX each have an independent
`struct virtqueue`, aligned backing pages, and ownership arrays.

`virtio_net` owns feature masks, the device MAC, RX/TX buffers, descriptor
metadata, completion records, counters, reset state, and the raw-frame test
hook. Its eventual M5-facing boundary is deliberately small:

```c
int virtio_net_send(const void *frame, u32 length);
int virtio_net_receive(void *frame, u32 capacity, u32 *length,
                       u64 deadline);
void virtio_net_get_stats(struct virtio_net_stats *stats);
```

Receive and send APIs copy frames. They return errors for invalid lengths,
failed/resetting devices, timeout, or exhausted resources. No M4 API exposes
descriptor addresses or transfers buffer ownership to callers.

## RX Ownership

The driver allocates a fixed RX slot for every queue descriptor. Each slot
contains a zeroed VirtIO net header followed by space for one Ethernet frame.
All RX slots are posted before `DRIVER_OK`; while posted, both the descriptor
and buffer belong to the device.

When an RX used entry arrives, the ISR validates the descriptor ID, active
ownership, used length, ten-byte header, and maximum Ethernet frame length.
It records only the completed slot ID in a fixed-capacity completion ring and
wakes a task-context consumer. The ISR does not allocate, sleep, parse a
protocol, or copy into a future `pktbuf`.

In task context, `virtio_net_receive` removes one completion, copies the frame
to its caller, resets the slot, and immediately reposts the same descriptor.
Malformed or overflow frames increment counters and are reposted without
delivery. A full completion ring drops the frame, increments a bounded-drop
counter, and reposts the slot without blocking.

## TX Ownership

TX uses a fixed bounce-buffer slot per available descriptor. A sender waits on
a TX-space queue when no slot is free. It copies a zeroed VirtIO net header and
the Ethernet frame into the selected slot, publishes one descriptor to queue
1, notifies the device, and may then reuse its source buffer.

The TX descriptor and bounce slot remain device-owned until the used ring
returns the descriptor ID. The ISR validates ownership, reclaims both exactly
once, increments completion/error counters, and wakes tasks waiting for TX
space. TX completion does not imply the TAP peer accepted the frame; the raw
test protocol proves end-to-end delivery through a returned response frame.

## Interrupt and Concurrency Model

One net-device spinlock protects device state, queue ownership, completion
indexes, counters, and reset transitions. `task_sleep` atomically drops this
lock when waiting for RX data or TX space. The ISR acknowledges the MMIO
interrupt first, takes the net lock, drains both used rings, records bounded
completion state, performs wakeups, and releases the lock.

The ISR performs no allocation and no unbounded copy. Lock ordering does not
nest the block and net locks. Block and net can complete on different harts,
and both use the existing task wake/IPI path.

## Reset and Failure Handling

Reset is explicit and serialized:

1. mark the device resetting and reject new sends;
2. fail and wake RX/TX waiters;
3. acknowledge pending device interrupts;
4. reset transport status and discard stale completion records;
5. rebuild both queue ownership maps and repost every RX buffer;
6. repeat feature negotiation and queue registration;
7. set `DRIVER_OK` and reopen submissions.

Invalid used IDs, duplicate completions, corrupted ownership, or impossible
lengths mark the driver failed and produce one stable failure marker. Normal
TX exhaustion and empty RX queues are backpressure, not panics. A failed reset
leaves the device stopped and wakes all waiters with an error.

## TAP and Raw Peer

`scripts/tap-up.sh` creates `tap0`, assigns `192.168.100.1/24`, and brings the
link up. `scripts/tap-down.sh` removes it. Both scripts are idempotent, accept
an interface-name override, and print the privileged command that failed.
They do not change Windows networking, forwarding, NAT, firewall policy, or
the default route.

Automated TAP acceptance uses a dedicated local interface and a Python
standard-library `AF_PACKET` peer. The peer filters only the M4 EtherType,
validates source/destination MAC, sequence, length, deterministic bytes, and
checksum, and returns a response to the guest MAC. Tests clean up the TAP and
QEMU processes on success, failure, signal, or timeout.

TAP creation requires Linux `CAP_NET_ADMIN` or `sudo`; raw packet access
requires `CAP_NET_RAW` or `sudo`. Missing privilege is reported as an explicit
environment failure, not mistaken for a driver failure. Host-only script and
state tests remain unprivileged.

The `192.168.100.0/24` addresses are reserved for M5 IP tests. M4 records the
address on TAP for continuity with the project design but does not claim Ping
or ARP success.

## Deterministic Guest Test

The PID 0 scheduled test initializes the net device after interrupts and the
scheduler are available. For each iteration it sends one deterministic raw
frame, sleeps for the matching response, validates the returned sequence and
payload, and verifies queue/buffer baselines. The guest does not busy-poll the
ISR or parse IP packets.

Stable M4 markers are:

```text
QS:NET_LINK_OK
QS:NET_IRQ_OK
QS:NET_TX_OK
QS:NET_RX_OK
QS:NET_RESET_OK
QS:NET_RESETS:<count>
QS:NET_STRESS_FRAMES:<count>
QS:TEST_PASS:m4-smoke
```

The quick profile uses a small deterministic frame count and one reset. The
stress profile requires at least 1000 bidirectional frames, ten resets, no
failure marker, no descriptor or buffer leak, and all existing M3 stress
baselines. QEMU success still requires guest-driven exit status 0 through the
test device.

## Testing

Host source-contract tests verify the board addresses, IRQs, two QEMU buses,
DTS nodes, rejected feature set, queue numbers, bounded completion ring,
nonblocking ISR rules, exact markers, and TAP cleanup behavior.

Sanitizer-backed C tests exercise RX/TX ownership transitions, invalid and
duplicate used IDs, length boundaries, completion-ring overflow, descriptor
exhaustion, reset reclamation, and ring wraparound without requiring TAP.

Fake-QEMU tests require every M4 marker and exit status 0, reject missing or
malformed markers, and retain all older marker requirements. Real QEMU tests
cover device discovery, MAC readback, both interrupt paths, raw TAP traffic,
reset, and descriptor/buffer baseline restoration. M1-M3 build and smoke
targets must continue to pass without a net device attached.

## Deferred

- Ethernet parsing and `pktbuf` integration.
- ARP, IPv4, ICMP, guest addressing, and Ping, which belong to M5.
- UDP/TCP, sockets, DNS, TFTP, HTTP, and NTP.
- Checksum or segmentation offload, control virtqueues, multiqueue, mergeable
  receive buffers, modern VirtIO MMIO v2, packed rings, and zero-copy I/O.
- Windows-host-to-guest exposure, bridging, NAT, and public network access.
