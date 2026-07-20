# M6C2 Passive TCP Design

## Goal

Add the first passive TCP server path to the existing first-party C network
stack. A guest process binds, listens, accepts one connection from the TAP
peer, echoes one reliable payload, and closes without leaking socket handles,
TCP PCBs, timers, semaphores, or packet buffers.

M6C2 reuses the M6C1 TCP input, output, retransmission, receive-buffer, close,
network-worker, and generation-checked socket machinery. It does not add a
second TCP/IP implementation.

## Scope

M6C2 includes:

- TCP `bind`, `listen`, and blocking `accept`;
- `LISTEN` and `SYN_RECEIVED` TCP states;
- one listening TCP endpoint in the functional milestone;
- a fixed TCP PCB storage pool shared by active, listening, half-open, and
  accepted connections;
- a listener-owned accept queue with backlog values from one through four;
- one server-side Echo connection in the QEMU/TAP functional acceptance;
- SYN-ACK and Echo retransmission through the existing one-outstanding-segment
  timer machinery;
- deferred FIN when `close` begins while sent data is still unacknowledged;
- deterministic cleanup for half-open and unaccepted children.

The functional milestone does not include RST handling, TCP options, SYN
cookies, out-of-order reassembly, multiple listening endpoints, IPv6, or the
eight-connection stress target. Concurrent connections and 100 reconnects are
added by the later `m6c2-stress` slice after the functional path is stable.

## Source And Ownership

The implementation extends the current M6C1 first-party C code under
`kernel/src/net`. The repository records the original tiny stack revision in
`docs/source-migration.md`; no new external TCP stack or copied third-party
network core is introduced.

This design does not claim a new line-for-line import from the old repository.
Any source comparison must use the pinned revision already recorded by the
project and preserve this design's ownership and acceptance contracts.

## Object Ownership

TCP PCBs need stable addresses because they contain locks, semaphores, timers,
receive storage, and outstanding packet ownership. They must never be copied
when `accept` creates a user-visible socket.

M6C2 replaces caller-owned TCP PCB storage with a fixed pool of eight PCB
objects owned by `tcp.c`. Active sockets, listeners, half-open children, and
accepted children all allocate from this pool. The existing TCP table keeps
the registered pool pointers used for tuple lookup and timer callbacks.

TCP socket entries store a `tcp_pcb_t *`; UDP entries retain their existing
embedded `udp_pcb_t`. An active `socket()` allocates a PCB immediately. A
passive child allocates a PCB when a valid SYN reaches a listener, but consumes
no socket-table entry until `accept` returns it to user space.

The invariants are:

- every allocated TCP PCB appears in the TCP table exactly once;
- an unaccepted child belongs to exactly one listener;
- an accepted child has no listener owner and lives until its own close;
- an accept queue contains only `ESTABLISHED`, unaccepted children;
- a PCB is returned to the pool only after its waiters, timers, outstanding
  packet, and listener links are gone;
- closing a listener never closes an already accepted child.

The functional milestone keeps `TCP_PCB_MAX` at eight. The later stress slice
must raise the bound to at least nine before claiming eight simultaneous
accepted connections plus one listener.

## Listener And Handshake State

`tcp_bind` records the default active netif, local IPv4 address, and local port
on a closed TCP PCB. Duplicate live TCP binds to the same address and port are
rejected. `tcp_listen` requires a bound, closed PCB, no other live listener,
and a backlog from one through four, then enters `TCP_STATE_LISTEN`.

TCP input performs exact four-tuple lookup first. If no connected PCB matches,
a pure SYN may match a listener by netif, destination address, and destination
port. If the listener backlog and PCB pool both have capacity, input allocates
a child, records the peer tuple, enters `TCP_STATE_SYN_RECEIVED`, sends SYN-ACK,
and arms the existing retransmission timer.

The final ACK must contain no payload, acknowledge the child ISS plus one, and
use the expected peer sequence. A valid ACK clears the SYN-ACK outstanding
record, moves the child to `ESTABLISHED`, appends it once to the listener accept
queue, and wakes one accept waiter. Invalid ACKs do not enqueue the child.

The backlog counts both `SYN_RECEIVED` children and established children still
waiting in the accept queue. When the backlog or PCB pool is full, input drops
the SYN and returns a resource error. M6C2 does not fabricate a handshake or
send an unimplemented RST; the peer may retry.

If SYN-ACK retries are exhausted, the child is detached from its listener and
fully released. Closing a listener performs the same cleanup for every
half-open or queued child.

## Accept Queue And Blocking

Each listener owns a four-entry ring of child PCB pointers, an accept semaphore,
and an accept-waiter count. The queue is protected by the listener state lock;
TCP table membership remains protected by the TCP table lock.

The socket layer follows the existing M6C1 waiter-pinning pattern:

1. validate the listening socket while holding the socket-table lock;
2. acquire an accept waiter on the listener before releasing that lock;
3. wait outside the socket-table and network-worker locks;
4. after wakeup, verify a free socket entry exists before consuming the queue;
5. peek the queue head and complete any requested peer-address copy;
6. only after the copy succeeds, dequeue the child, detach it from the
   listener, and attach the stable PCB pointer to a new generation-checked TCP
   handle;
7. release the waiter on every success, timeout, close, or error path.

If the socket table is full, `accept` returns `NET_ERR_FULL` and leaves the
queued child untouched. Closing the listener wakes all accept waiters; they
return a state error without consuming the queue or touching freed semaphores.

## Socket ABI

M6C2 adds the standard Linux RISC-V syscall numbers:

- `__NR_listen = 201`;
- `__NR_accept = 202`.

The user API is:

```c
int sys_listen(int fd, int backlog);
int sys_accept(int fd, net_sockaddr_in *address, size_t *address_length);
```

`bind` is extended to dispatch by socket type. UDP retains its current port
binding behavior. TCP bind uses the supplied local address and the default
netif; the address must be either the netif IPv4 address or the wildcard value.

`accept` permits both address arguments to be null. If either is non-null, both
must be present, the supplied length must cover `net_sockaddr_in`, and both
user ranges must be writable. These checks happen before blocking and before
dequeueing a child. A failed user copy never consumes the accept queue.

`listen` and `accept` reject UDP sockets. A listening TCP socket rejects
`connect`, `send`, and `recv`; a connected TCP socket rejects `listen` and
`accept`. Invalid handles, duplicate bind, invalid backlog, invalid user
pointers, incompatible states, and exhausted pools return existing negative
network errors and never call `panic`.

## Send And Close Behavior

Accepted connections reuse the M6C1 byte receive queue and one-segment send
path. The server Echo is bounded by `TCP_MSS`.

If `close` starts while an established connection still owns unacknowledged
data, TCP records a close request instead of returning `NET_ERR_FULL`. When the
data ACK clears the outstanding segment, the network worker sends FIN and the
existing FIN-WAIT/TIME-WAIT path completes the blocking close. This behavior
also improves active M6C1 sockets without changing their public ABI.

Listener close does not send FIN. It marks the listener terminal, wakes accept
waiters, cancels and releases every unaccepted child, removes the bound port,
and releases the listener after its waiters drain.

## Guest And TAP Flow

The M6C2 guest chain remains cumulative:

```text
udp_echo -> tcp_echo -> tcp_server_echo
```

`tcp_server_echo` binds `192.168.100.2:4801`, listens with backlog four,
accepts one peer, receives the fixed payload, sends the exact bytes back, then
closes the accepted socket and listener.

After the existing M6C1 active-client flow completes, `m5-peer.py` becomes an
active client for the guest server. It sends SYN, validates SYN-ACK, sends the
final ACK and payload, validates the guest ACK and Echo, deliberately withholds
the first Echo ACK, observes a byte-identical retransmission, acknowledges it,
and completes the FIN exchange. Peer statistics distinguish the M6C1 active
flow from the M6C2 server flow.

## Acceptance Evidence

The functional acceptance requires exactly one of each marker, in order:

```text
QS:M6C2_LISTEN_OK
QS:M6C2_ACCEPT_OK
QS:M6C2_ECHO_OK
QS:M6C2_CLOSE_OK
QS:TEST_PASS:m6c2-smoke
```

Listen and accept markers are printed by the guest only after their syscalls
succeed. The Echo completion bit is set only after the peer acknowledges a
real retransmission of the server payload. The close marker is printed once by
the kernel before it sets the final cleanup bit, so the final pass cannot race
ahead of visible cleanup evidence.

The TAP peer JSON must additionally prove at least one server-side SYN,
handshake, data request, Echo retransmission, and FIN, with zero outstanding
server records. All M0-M6C1 markers and peer statistics remain required.

## Host Tests

Host coverage is split by responsibility:

- TCP pool tests preserve M6C1 active allocation, exhaustion, reuse, timers,
  and stale-pointer behavior after the storage refactor;
- passive TCP tests cover listener lookup, SYN-ACK, invalid final ACK, backlog
  saturation, half-open retransmission exhaustion, queue ordering, and listener
  cleanup;
- socket tests cover type checks, generation changes, blocking accept, close
  wakeups, a full socket table, and queued-child preservation on errors;
- syscall contracts verify numbers, wrappers, dispatch, address validation,
  and validation-before-consumption;
- peer tests cover correct sequence/ACK/flags/payload handling and reject
  malformed handshake, Echo, retransmission, and FIN traffic;
- smoke-script tests reject missing, duplicate, late, or fabricated markers,
  incomplete UDP/M6C1 evidence, incomplete M6C2 statistics, and nonzero peer or
  QEMU exits.

All existing M0-M6C1 host tests remain regression requirements. Target build
and real TAP acceptance are reported separately from host-only evidence and
are never claimed when the RISC-V toolchain, submodules, TAP, or QEMU artifacts
are unavailable.

## Completion Criteria

M6C2 functional work is complete only when:

1. the full host suite passes on the supported Ubuntu environment;
2. `make m6c2-build` succeeds from a clean output tree;
3. `make m6c2-smoke` exits zero with all cumulative markers and peer evidence;
4. the listener, child, accept-waiter, timer, packet, and socket lifetime paths
   pass a strict code review;
5. the result is explicitly described as functional single-connection M6C2,
   while eight-connection and reconnect stress remain assigned to
   `m6c2-stress`.
