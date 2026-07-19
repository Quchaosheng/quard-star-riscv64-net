# M6C1 Active TCP Design

## Goal

Add the smallest user-visible TCP slice to the existing C network stack: an
active TCP client that can connect to a TAP peer, exchange a reliable byte
stream, survive one deliberately lost data acknowledgement, and close
cleanly. The passive server path remains M6C2.

M6C1 is a first-party C reimplementation aligned with the documented
`tiny-tcpip-stack` baseline. The fixed source revision
`32e4988e2d482ad3ee406e36b5adbd84a63c8e9e` is recorded in
`docs/source-migration.md`, but the upstream repository is currently
unavailable from the development environment; no claim is made that files
were copied verbatim.

## Scope Boundary

M6C1 includes:

- TCP over the existing IPv4, packet-buffer, timer, loopback, and VirtIO/TAP
  layers;
- active `connect`, stream `send`, stream `recv`, and `close` operations;
- fixed-size TCP PCB and socket tables;
- SYN/SYN-ACK/ACK handshake, sequence and acknowledgement validation,
  advertised receive window, FIN close, and TIME-WAIT cleanup;
- one outstanding segment per PCB, bounded retransmission, and timeout
  wakeups;
- host tests for headers, pseudo-header checksum, state transitions,
  retransmission, receive ordering, close, and resource restoration;
- a real TAP peer and `user/tcp_echo.c` acceptance program.

M6C1 does not include `listen`, `accept`, a passive server PCB, concurrent
server connections, out-of-order reassembly, SACK, congestion control,
window scaling, IPv6, TLS, or large-file streaming. Those are M6C2 or later.

## Architecture

The existing network worker remains the only context that mutates TCP
protocol state. User syscalls submit socket mutations through the existing
bounded network executor. Blocking stream operations wait on per-PCB queues
and semaphores; they never hold a protocol lock while sleeping.

```text
socket syscall
  -> generation-checked TCP socket handle
  -> network request executor
  -> TCP PCB operation
  -> IPv4 output / TCP input
  -> VirtIO Ethernet or loopback
```

TCP input is registered under IPv4 protocol 6. The handler validates the
fixed header, data offset, flags, sequence range, advertised window, and
pseudo-header checksum before changing a PCB. Invalid or unsupported packets
are freed by the existing stack ownership path and cannot wake a user
waiter.

## TCP PCB Contract

Each PCB owns:

- local and remote IPv4 addresses and ports;
- current state and initial sequence number;
- `snd_una`, `snd_nxt`, `rcv_nxt`, and bounded send-window information;
- one transmit record for the outstanding segment;
- a byte receive queue with a fixed storage limit;
- connect, receive, and close wait state;
- one retransmission timer and retry counter.

The first implementation supports eight TCP PCBs. A handle is invalidated
before the PCB slot can be reused. Close releases queued bytes, transmit
records, timers, and waiters exactly once.

## State And Reliability Rules

The active path supports these transitions:

```text
CLOSED -> SYN_SENT -> ESTABLISHED -> FIN_WAIT_1 -> FIN_WAIT_2 -> TIME_WAIT -> CLOSED
```

An incoming SYN-ACK must acknowledge the SYN sequence. Data is accepted only
when its sequence equals `rcv_nxt`; accepted bytes advance `rcv_nxt` and
produce an ACK. A segment with an earlier sequence is acknowledged again and
discarded; a future sequence is acknowledged without being queued. This
keeps the first slice deterministic without pretending to implement
out-of-order reassembly.

Only one data or control segment may be outstanding. A retransmission timer is
armed for SYN, data, FIN, and any segment requiring acknowledgement. It uses
the shared monotonic network timer, retransmits after 500 ms, and fails the
PCB after five attempts. Every timeout path wakes the blocked syscall and
frees the outstanding packet exactly once.

The send path copies user bytes into bounded packet storage before submitting
the network request. The receive path removes bytes from the PCB queue only
after all user output ranges have been validated. A failed user copy returns
an error while preserving queued bytes.

## Socket And Syscall Surface

M6C1 extends the existing ABI with TCP type selection and:

- `connect(fd, sockaddr_in, length)`;
- `send(fd, buffer, length, flags)`;
- `recv(fd, buffer, length, flags)`;
- existing `close(fd)` for TCP handles.

UDP operations remain unchanged. Unsupported domains, flags, address lengths,
zero-length or oversized buffers, invalid handles, wrong socket type, and
state-incompatible operations return negative errors without changing TCP
state. The socket layer owns handle generation and lifetime; the network
worker owns PCB mutation.

## TAP Peer And Acceptance

`scripts/m5-peer.py` gains a minimal raw TCP peer for one client connection.
It responds to the SYN, validates the guest sequence and checksum, echoes the
payload, intentionally drops the first valid data ACK, then acknowledges the
retransmission. It completes FIN exchange and records packet counts in the
existing JSON statistics file.

`user/tcp_echo.c` connects to `192.168.100.1:4800`, sends a bounded payload,
reads the exact echo, waits for the retransmission-backed acknowledgement,
and closes. The kernel selftest bits are set from successful syscall results,
not from printed strings. Required markers are:

```text
QS:M6C1_TCP_OK
QS:M6C1_TCP_RETRANS_OK
QS:M6C1_TCP_CLOSE_OK
QS:TEST_PASS:m6c1-smoke
```

The smoke script must also require peer-observed SYN/SYN-ACK/ACK, at least one
guest data retransmission, a complete echo, FIN exchange, and zero outstanding
peer records at shutdown.

## Verification

Host tests are split by contract:

- `test_m6c1_tcp.c`: header length, flags, checksum, sequence acceptance,
  state transitions, and close behavior;
- `test_m6c1_retrans.c`: timer expiry, retry limit, packet ownership, and
  wakeup behavior;
- `test_m6c1_socket.c`: TCP handle generation, type checks, and slot reuse;
- shell contracts: syscall ABI, build flags, peer statistics, exact marker
  counts, and smoke-script failure behavior.

The acceptance sequence is:

1. `make test-host` passes on Ubuntu 24.04.
2. `make m6c1-build` builds the RISC-V firmware from a clean output tree.
3. `make m6c1-smoke` exits zero with all M0-M6B markers, the four M6C1
   markers, peer TCP statistics, and no QEMU timeout.
4. A strict review checks packet ownership, user-copy preservation, timer
   cancellation, close wakeups, and truthful marker setting.

## Follow-up: M6C2

M6C2 adds passive `listen`/`accept`, a bounded accept queue, multiple active
connections, server-side echo, reconnect loops, and concurrent TCP stress.
It reuses the M6C1 TCP PCB and retransmission machinery rather than adding a
second implementation.
