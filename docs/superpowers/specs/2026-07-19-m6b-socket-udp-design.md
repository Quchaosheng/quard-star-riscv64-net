# M6B Socket And UDP Design

## Goal

Add the first user-facing socket slice to the existing C network stack: a
single-threaded network request executor, UDP protocol control blocks, and a
minimal socket syscall path that can be proven with host tests and QEMU/TAP.

M6B deliberately stops before TCP. TCP state machines, retransmission, listen,
accept, and connect follow in M6C.

## Source Boundary

The protocol behavior is migrated from the author's `tiny-tcpip-stack` only.
The RISC-V port keeps the current IPv4, ARP, loopback, packet-buffer, timer,
and VirtIO layers. No lwIP, smoltcp, or second TCP/IP implementation is added.

## Architecture

The network worker remains the only context allowed to mutate UDP protocol
state. Callers submit one synchronous request at a time through the existing
network request executor; the executor wakes the worker and waits on a result
semaphore. It is the sole request queue for M6B.

```text
socket syscall
  -> socket handle
  -> network request executor
  -> UDP PCB operation
  -> IPv4 output / UDP input
  -> Ethernet ARP or loopback
```

The first implementation uses a bounded kernel socket table and a testable
handle type. A later fd-table integration may wrap the same socket object; it
does not change UDP ownership or protocol state.

## UDP Contract

Each UDP PCB owns:

- local address and port;
- optional connected peer address and port;
- receive queue of packet-plus-source records;
- closed flag and one receive waiter.

`udp_bind` rejects an occupied local port unless the existing stack explicitly
supports reuse. `udp_sendto` validates payload length and destination, copies
the payload into a packet buffer, and transfers ownership to IPv4 output.
`udp_recvfrom` copies one datagram and its source tuple to the caller, removes
the record only after successful copy, and preserves the datagram on a failed
user-copy operation.

Timeout semantics match M6A:

- timeout `< 0`: return immediately with would-block;
- timeout `== 0`: wait forever until data or close;
- timeout `> 0`: wait for monotonic milliseconds and return timeout.

Closing a socket wakes its receiver, releases queued datagrams exactly once,
removes the PCB from the bound-port table, and invalidates the handle. All
allocation and queue exhaustion paths return errors without panic.

## Syscall Surface

M6B adds only the minimum operations required for UDP Echo:

- `socket(domain, type, protocol)`;
- `bind(fd, address, address_length)`;
- `sendto(fd, buffer, length, flags, address, address_length)`;
- `recvfrom(fd, buffer, length, flags, address, address_length)`;
- `close(fd)`.

The ABI uses fixed-width kernel address structures and explicit length checks.
Unknown flags, unsupported families, invalid handles, and truncated user
addresses return a negative error. Existing `read`, `write`, `exec`, and wait
syscalls remain unchanged.

## Input And Output Ownership

The UDP input path validates the UDP header length and checksum before creating
a receive record. Malformed datagrams are freed immediately. A successful
record insertion transfers packet ownership to the PCB queue; queue-full input
is dropped and counted. Output consumes its packet on every return path through
the existing IPv4/netif ownership contract.

## Verification

Host tests cover:

- bind success and duplicate-port rejection;
- send/receive source tuple and payload preservation;
- malformed and checksum-invalid datagram rejection;
- nonblocking, permanent, and timed receive;
- close wakeup, handle invalidation, and packet-pool restoration;
- executor serialization under two concurrent callers.

QEMU/TAP acceptance reuses the M6A peer and adds exact markers:

```text
QS:M6B_UDP_OK
QS:M6B_UDP_TIMEOUT_OK
QS:TEST_PASS:m6b-smoke
```

The real smoke must prove bidirectional UDP Echo, one timed receive timeout,
and zero leaked packet/socket records at shutdown.

## Out Of Scope

M6B does not add TCP, DNS, TFTP, HTTP, NTP, IPv6, fragmentation, routing
tables, socket options, `listen`, `accept`, `connect`, or a second request
queue.
