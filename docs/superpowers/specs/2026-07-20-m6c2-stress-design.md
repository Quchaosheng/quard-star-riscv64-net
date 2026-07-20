# M6C2 TCP Stress Design

## Goal

Extend the existing first-party passive TCP path with deterministic evidence
for eight simultaneously accepted connections and 100 sequential reconnects.
The stress slice reuses the M6C2 server, TCP PCB pool, socket table, TAP peer,
and QEMU acceptance flow. It does not add another TCP implementation.

## Scope

The stress slice adds:

- capacity for one listener and at least eight accepted TCP children;
- a guest workload that keeps eight accepted connections alive together;
- 100 additional accept, Echo, peer-close, and release cycles;
- a multi-connection peer state machine with per-tuple sequence tracking;
- kernel counters for accepted, live, peak-live, and released children;
- a dedicated `m6c2-stress` build-and-acceptance target;
- host tests for capacity, counters, peer validation, and smoke rejection.

It does not add RST handling, TCP options, SYN cookies, out-of-order
reassembly, IPv6, multiple listeners, or a general-purpose benchmark tool.

## Capacity And Ownership

Raise `TCP_PCB_MAX` from eight to twelve. Eight accepted children plus one
listener require nine slots. Three spare slots keep the cumulative M6C1
client and deferred release activity from depending on exact scheduling while
retaining a small fixed embedded pool. `NET_SOCKET_MAX` remains sixteen.

No ownership rules change. Passive children remain listener-owned until
accept commit, accepted children remain Socket-owned until detach, and only
the network worker may release timer-owned PCBs.

Host tests must prove:

- twelve PCBs can be allocated and the thirteenth fails;
- eight accepted children and one listener can coexist;
- closing and timer processing make every slot reusable;
- the existing single-connection M6C2 tests still pass.

## Guest Workload

`tcp_server_echo` selects its existing one-connection workload unless
`QS_M6C2_STRESS` is defined.

The stress workload has two phases:

1. Accept eight connections. For each connection, receive and echo one
   connection-specific payload, then retain the accepted descriptor. After
   all eight Echo replies are acknowledged, observe peer FIN and close every
   descriptor.
2. Repeat accept, receive, Echo, peer FIN, and close 100 times using one
   connection at a time.

The peer initiates close after the Echo acknowledgement. This exercises the
passive close path and avoids one second of TIME_WAIT per reconnect. The guest
closes the listener only after all 108 children have closed.

The payload includes the connection index so an Echo from one tuple cannot be
credited to another tuple.

## Peer State Machine

Extend `scripts/m5-peer.py` with explicit stress arguments rather than a
second peer implementation. Each connection is stored by host source port and
tracks host sequence, guest sequence, phase, payload, and outstanding data.

The peer opens the first eight connections one at a time but does not send
FIN until all eight have completed Echo. It then closes those eight and runs
100 sequential reconnects. Every SYN-ACK, data ACK, Echo, FIN, and final ACK
must match that connection's tuple and sequence space.

Stress statistics include:

- `tcp_server_stress_handshakes` equal to 108;
- `tcp_server_stress_echo` equal to 108;
- `tcp_server_stress_parallel_peak` equal to eight;
- `tcp_server_stress_reconnects` equal to 100;
- `tcp_server_stress_fin` equal to 108;
- `tcp_server_stress_outstanding` equal to zero.

Malformed, duplicate, cross-tuple, incomplete, or late traffic fails the peer.
The existing one-connection mode and statistics remain unchanged.

## Kernel Evidence

The 32-bit cumulative completion mask is already full, so stress evidence
uses separate atomic counters rather than assigning fake completion bits.
Under `QS_M6C2_STRESS`:

- accept commit increments accepted and live-child counters;
- accepted-child publication from the PCB pool decrements live and increments
  released;
- the maximum observed live-child count records the parallel peak;
- selftest PASS remains gated until accepted and released are at least 108,
  live is zero, and peak is at least eight.

The kernel prints one parallel marker and one reconnect marker only after the
counter gate is satisfied. `QS:TEST_PASS:m6c2-stress` must be printed after
both markers, never by a generic helper or user-space print alone.

## Build And Acceptance

`m6c2-stress` follows the existing M2C, M3, and M4 stress pattern: its script
sets stage `m6c2-stress`, adds `QS_M6C2_STRESS`, performs the cumulative build,
then reuses the smoke infrastructure with stress peer arguments and a longer
timeout. Its output directory remains isolated from `m6c2`.

Host acceptance covers:

- PCB capacity and full reuse;
- guest workload constants and error paths;
- peer success plus corrupted tuple, sequence, payload, count, and close
  cases;
- smoke rejection for missing or duplicate stress markers, counts below the
  thresholds, nonzero outstanding connections, early PASS, peer failure, and
  QEMU failure;
- all existing M0-M6C2 host tests.

Target acceptance requires a clean real `make m6c2-stress` on Linux with the
RISC-V toolchain, initialized submodules, QEMU, and TAP access. Host tests and
fake-QEMU script tests are not substitutes for that evidence.

## Completion Criteria

The stress slice is complete only when:

1. the full host suite passes;
2. the target stress build succeeds as part of `make m6c2-stress`;
3. real QEMU/TAP acceptance reports exactly 108 validated Echo connections,
   a parallel peak of eight, 100 reconnects, and zero outstanding sessions;
4. all PCB, timer, packet, semaphore, listener, and Socket resources are
   released;
5. strict review finds no fabricated marker or bypass of the acceptance gate.
