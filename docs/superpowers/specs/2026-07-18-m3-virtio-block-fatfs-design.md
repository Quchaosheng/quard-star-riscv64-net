# M3 VirtIO Block and FatFs Design

## Goal

Build a reusable legacy VirtIO MMIO v1 and virtqueue layer, move the existing
block driver to interrupt-driven sleeping I/O, and verify it through a pinned
FatFs R0.15 test volume. Keep every M1 and M2 build and smoke target intact.

## Delivery Split

M3 is delivered as three checkpoints on `codex/m3-virtio`:

- M3A extracts common MMIO negotiation and split-ring management from the
  current block driver.
- M3B converts block requests from polling to interrupt-driven completion and
  adds concurrent-I/O stress coverage.
- M3C adds the pinned FatFs sources, a small disk port, a deterministic test
  image, and an end-to-end filesystem smoke test.

Each checkpoint must build and pass its focused tests before the next begins.

## Supported Transport

- Keep the existing legacy VirtIO MMIO v1 transport. Queue registration uses
  guest page size, queue size, queue alignment, and queue PFN.
- Do not mix modern MMIO v2 queue-address registers into the same driver.
  A v2 migration remains a separate later milestone.
- M3 uses the existing block transport at `0x10100000`, IRQ 1, and queue 0.
  Adding the second transport for VirtIO net remains M4.
- Common MMIO code validates magic, version, device ID, vendor ID, queue
  availability, queue capacity, and the `FEATURES_OK` status readback.

## Module Boundaries

`virtio_mmio` owns transport register access and the device status sequence:
reset, acknowledge, driver, feature negotiation, `FEATURES_OK`, queue setup,
and `DRIVER_OK`. The block driver supplies its expected device ID and feature
mask; transport code does not contain block request knowledge.

`virtqueue` owns one legacy split ring and its descriptor state. It provides
descriptor-chain allocation, rollback on partial allocation, avail-ring
publication, used-ring consumption, and chain reclamation. It validates every
device-provided head ID before indexing driver state. Queue storage remains a
page-aligned two-page region for the current eight-entry queue.

`virtio_blk` owns block request headers, buffer direction, sector conversion,
per-request status, and request completion. It uses one queue instance and one
driver lock; no queue state is shared with future network queues.

The block layer exposes a sector-based internal transfer function for FatFs.
The existing public `virtio_disk_init`, `virtio_disk_rw`, and
`virtio_disk_intr` entry points remain available; `virtio_disk_rw` becomes a
thin two-sector wrapper for the current 1024-byte buffer-cache blocks.

## Descriptor Lifecycle

Each block request reserves exactly three descriptors: request header, data,
and one-byte status. Allocation either returns all three descriptors or rolls
back every descriptor reserved by that attempt.

Before notifying the device, the driver initializes request metadata, records
the request under the chain head, publishes the head to the avail ring, and
uses a full memory barrier before updating the avail index and notifying queue
0. The descriptor chain remains owned by the device until its head appears in
the used ring.

Completion validates that the used ID is in range, refers to an active chain,
and has a successful block status. The driver then clears request ownership,
reclaims the full chain exactly once, and wakes both the completed requester
and any task waiting for descriptors.

## Interrupt and Sleeping Model

- `virtio_disk_rw` takes the block lock before inspecting descriptor state.
  If three descriptors are unavailable, it sleeps on a descriptor-available
  channel while atomically releasing that lock.
- After submission, the caller sleeps on its request-completion channel. It
  never calls `virtio_disk_intr` as a polling function.
- The external interrupt handler acknowledges the VirtIO MMIO interrupt and
  drains all newly used entries while holding the block lock.
- The ISR performs bounded queue bookkeeping and wakeups only. It does not
  allocate memory, enter FatFs, parse filesystem data, or wait for resources.
- A completion wakeup uses the M2 wait primitives and scheduler IPI path when
  another hart is idle.

The block lock is always acquired before a task sleeps, and `task_sleep`
acquires the task-table lock before dropping the block lock. This preserves the
existing no-lost-wakeup rule. No path may acquire the block lock while already
holding the task-table lock.

## Buffer Cache Scope

M3 protects the existing buffer-cache list and reference counts with one
sleeping metadata lock. Each buffer also has a sleeping data lock so two tasks
cannot fill or modify the same block concurrently. Cache misses, LRU selection,
reference changes, and list moves are serialized. The metadata lock is
released before acquiring a buffer data lock or waiting for device I/O, so
unrelated cached blocks remain usable.

This milestone does not redesign the cache, add writeback, or add multiple
block devices.

## FatFs Integration

- Use only the official R0.15 archive pinned by `third_party/fatfs.lock`.
  Extraction verifies the recorded SHA-256 before copying the required FatFs
  source and license files to the ignored `out/deps/fatfs` build directory.
  Upstream FatFs files are compiled from there and are not committed into the
  first-party kernel tree.
- Keep extracted FatFs source unchanged. First-party adaptation lives in a
  separate kernel disk I/O port and configuration file.
- Expose one logical drive backed by the existing VirtIO block device. Sector
  size is 512 bytes; FatFs reads and writes map to the block driver without
  bypassing its synchronization.
- Support mount, create/open, read, write, seek, sync, and close operations
  needed by the M3 test. Optional filesystem features remain disabled.
- Filesystem and media errors return FatFs result codes. They produce a stable
  test failure marker during smoke tests but do not panic the kernel.

## Deterministic Test Volume

The M3 build script creates a fresh FAT test image using fixed geometry and
fixed input data. Generated images remain under `out/` and are not committed.
The test is independent of public network services and wall-clock time.

Device identity negotiation and queue initialization still run during kernel
boot. Real block and FatFs I/O run from the first init task after the scheduler
and interrupts are active but before that task enters user mode. Earlier M1/M2
targets use the same scheduled block readback path, so the driver has no
boot-only polling fallback or test-only syscall.

The guest mounts the volume, writes a file containing deterministic blocks,
closes it, reopens it, reads every byte, and verifies length and content. It
then repeats the workload and checks that every descriptor and pending-request
count returns to its baseline. The second hart handles scheduling and device
interrupts during the test; multi-request user file I/O remains a later API
milestone.

Stable markers are:

```text
QS:VIRTQUEUE_OK
QS:BLOCK_IRQ_OK
QS:BLOCK_STRESS_OK
QS:FATFS_OK
QS:TEST_PASS:m3-smoke
```

M3 success requires all markers, no `QS:TEST_FAIL`, and QEMU guest exit status
0 through the existing SiFive test device.

## Error Handling

Transport identity mismatches, rejected `FEATURES_OK`, invalid queue geometry,
out-of-range used IDs, duplicate completions, and corrupted descriptor chains
are kernel/device contract violations. The driver marks the device failed,
prints a stable failure code, and stops further submissions; initialization
failures may panic because the M3 test requires block storage.

Descriptor exhaustion is normal backpressure and causes sleep, not panic.
FatFs mount, file, and media errors return normally to the caller.

## Testing

Host source-contract tests verify module separation, legacy v1 registration,
used-ID validation, descriptor rollback, interrupt waiting, and the absence of
polling calls from `virtio_disk_rw`. Small host-compiled queue tests exercise
allocation, rollback, ring wraparound, invalid IDs, and full reclamation with
warnings and sanitizers enabled.

QEMU tests cover real block interrupts, repeated read/write requests,
descriptor wakeup, deterministic FatFs readback, guest-driven exit, and all
earlier M1/M2 smoke targets. A longer M3 stress target repeats the workload and
requires zero descriptors and requests left in flight.

## Deferred

- VirtIO MMIO v2 and packed rings.
- Multiple block queues, indirect descriptors, event index, and block feature
  extensions.
- The second VirtIO MMIO transport and VirtIO net, which belong to M4.
- FatFs-facing user syscalls and shared file abstractions for TFTP/HTTP, which
  are introduced with their consuming milestones.
