# M6A Network Runtime Design

## Goal

Migrate the first M6 runtime slice from `tiny-tcpip-stack` revision
`32e4988e2d482ad3ee406e36b5adbd84a63c8e9e`: blocking queue and pool
timeouts, the shared network timer, ARP aging/retry, and the loopback
interface. Socket, UDP, and TCP remain in the next slice.

## Source Boundary

The implementation is adapted only from these first-party baseline files:

- `code/pc/src/net/src/{fixq,mblock,timer,arp,loop}.c`
- `code/pc/src/net/net/{fixq,mblock,timer,arp,loop,sys}.h`

Windows, pthread, pcap, x86, and STM32 platform code is not migrated. The
RISC-V port uses the existing TimerOS scheduler, semaphore, monotonic timer,
and VirtIO-net path.

## Timeout Semantics

All migrated waits preserve the original stack contract:

- `timeout < 0`: do not block.
- `timeout == 0`: wait forever.
- `timeout > 0`: wait for that many monotonic milliseconds.

A timeout never transfers ownership of a message or memory block. Code must
release every spinlock before sleeping.

## Components

### Network platform layer

Add the small `sys_*` subset required by the migrated runtime:

- monotonic current time and elapsed milliseconds;
- fixed-pool semaphore create, wait, notify, and free;
- millisecond conversion to the existing `r_mtime()` deadline model.

The host build uses a POSIX implementation for behavioral tests. The target
implementation allocates semaphore objects from a bounded static pool and
returns an error when it is exhausted.

### Fixed queue and memory pool

Restore the baseline producer/consumer semaphore model. A queue has one
semaphore for free slots and one for queued messages. A shared memory block
pool has one semaphore for available blocks. `NLOCKER_NONE` pools remain
non-blocking because they are owned by the serialized network core.

### Shared network timer

Migrate the baseline delta-ordered timer list. Timer callbacks run only in the
network core thread. The worker advances the list with elapsed monotonic
milliseconds and limits its next VirtIO receive wait to the nearest timer.
This timer is also the future TCP retransmission timer; no ARP-only timer loop
is introduced.

### ARP lifecycle

Each ARP entry stores an aging counter and retry counter. A resolved entry
becomes pending when it expires and sends a new request. A pending entry
retries at the configured interval. When retries are exhausted, all queued
packets are freed exactly once and the entry returns to the pool. A valid ARP
reply refreshes the entry and sends its waiting packets.

### Loopback

Migrate the baseline loopback netif with `127.0.0.1/8`. IPv4 output returns to
the original layering:

```text
ipv4_out -> netif_out -> Ethernet ARP output or Loopback queue
```

Loopback output moves the packet from the interface output queue to its input
queue. The network worker drains that queue on its next iteration. It does not
call IPv4 recursively.

## Ownership And Errors

- Successful `fixq_send` transfers the message to the queue; failure does not.
- Successful `fixq_recv` transfers the message to the caller.
- `mblock_alloc` returns `NULL` on non-blocking exhaustion or timeout.
- ARP owns packets while they are on an unresolved entry.
- Loopback owns a packet after it enters the output queue and frees it if the
  input queue cannot accept it.
- Pool exhaustion, timeout, and queue-full conditions return errors; they do
  not panic.

## Verification

Host tests must cover:

- non-blocking, permanent, timed, and wake-before-timeout queue/pool waits;
- timer ordering, reload, removal, and elapsed-time advancement;
- ARP refresh, retry, failure cleanup, and reply completion;
- loopback ICMP Echo with queue and `pktbuf` pool counts restored afterward.

The real QEMU acceptance target is `m6a-smoke`. It retains the M5 TAP test and
adds these exact markers:

```text
QS:M6_QUEUE_OK
QS:M6_ARP_TIMER_OK
QS:M6_LOOP_OK
QS:TEST_PASS:m6a-smoke
```

## Out Of Scope

M6A does not add a routing table, Socket syscalls, user fd objects, UDP, TCP,
DNS, IP fragmentation, or a second network RPC queue. Those changes follow
only after this runtime slice passes host tests and real QEMU/TAP acceptance.
