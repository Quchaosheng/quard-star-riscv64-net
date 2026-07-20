# M6C2 Passive TCP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a bounded passive TCP server that binds, listens, accepts one TAP connection, echoes a retransmitted payload, and closes without leaking protocol or socket resources.

**Architecture:** Move TCP PCB storage into one fixed pool owned by `tcp.c`, while TCP socket entries retain stable PCB pointers. Add listener and half-open states, a four-entry accept queue, and a two-phase socket accept API so user-address copy completes before queue consumption. Reuse the M6C1 network worker, one-outstanding-segment retransmission, receive queue, and FIN/TIME-WAIT machinery.

**Tech Stack:** C11, RISC-V64 syscall ABI, existing TimerOS locks/semaphores/timers, fixed-size packet/socket/PCB pools, Bash host tests, Python AF_PACKET TAP peer, QEMU quard-star.

---

## File Map

- `kernel/include/timeros/net/tcp.h`: TCP states, pooled PCB shape, listener queue, and passive APIs.
- `kernel/src/net/tcp.c`: pooled allocation, passive handshake, accept queue, retransmission cleanup, and deferred close.
- `kernel/include/timeros/net/socket.h`: pointer-backed TCP entries and two-phase accept contract.
- `kernel/src/net/socket.c`: TCP bind/listen dispatch, waiter pinning, accept prepare/commit/abort, and handle attachment.
- `kernel/include/timeros/syscall.h`: `listen`/`accept` syscall IDs and user declarations.
- `kernel/lib/app.c`: user syscall wrappers.
- `kernel/src/syscall.c`: validated bind/listen/accept entry points and network-worker submission.
- `kernel/include/timeros/selftest.h`, `kernel/src/selftest.c`: truthful M6C2 completion bits and marker ordering.
- `user/tcp_server_echo.c`: one-connection passive Echo acceptance program.
- `user/tcp_echo.c`, `user/Makefile`: cumulative M6C1-to-M6C2 guest chain and image build.
- `scripts/m6c2-build.sh`, `scripts/m6c2-smoke.sh`: M6C2 stage wrappers.
- `scripts/m5-peer.py`, `scripts/m5-smoke.sh`: active peer flow, statistics, and cumulative acceptance.
- `tests/host/run_m6c2_tcp_test.sh`: shared sanitized build command for the new TCP host tests.
- `tests/host/test_m6c2_*.c`, `tests/host/test_m6c2_*.sh`: pool, TCP, socket, syscall, peer, and smoke evidence.
- `Makefile`: M6C2 targets and host-test registration.

### Task 1: Add M6C2 Build And Contract Scaffolding

**Files:**
- Create: `scripts/m6c2-build.sh`
- Create: `scripts/m6c2-smoke.sh`
- Create: `tests/host/test_m6c2_contracts.sh`
- Modify: `scripts/m6c1-build.sh`
- Modify: `scripts/m6c1-smoke.sh`
- Modify: `scripts/m6b-build.sh`
- Modify: `Makefile`

- [ ] **Step 1: Write the failing contract test**

Create `tests/host/test_m6c2_contracts.sh` with executable mode. It must require
the two Make targets, cumulative flags, wrapper reuse, ABI names, passive TCP
states, and server program:

```bash
#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
fail() { echo "FAIL: $*" >&2; exit 1; }

grep -q '^m6c2-build:' "$root/Makefile" || fail 'missing m6c2-build target'
grep -q '^m6c2-smoke:' "$root/Makefile" || fail 'missing m6c2-smoke target'
grep -qx 'export QS_M6C2_TEST=1' "$root/scripts/m6c2-build.sh" || \
  fail 'missing M6C2 build flag'
grep -q 'm6c1-build.sh' "$root/scripts/m6c2-build.sh" || \
  fail 'M6C2 build must reuse M6C1'
grep -qx 'export QS_STAGE=m6c2' "$root/scripts/m6c2-smoke.sh" || \
  fail 'missing M6C2 smoke stage'
grep -qx 'export QS_TEST_NAME=m6c2-smoke' "$root/scripts/m6c2-smoke.sh" || \
  fail 'missing M6C2 smoke name'
grep -q 'm6c1-smoke.sh' "$root/scripts/m6c2-smoke.sh" || \
  fail 'M6C2 smoke must reuse M6C1'
for name in listen accept; do
  grep -q "^#define __NR_$name " "$root/kernel/include/timeros/syscall.h" || \
    fail "missing __NR_$name"
  grep -q "case __NR_$name:" "$root/kernel/src/syscall.c" || \
    fail "missing $name dispatch"
  grep -q "sys_$name" "$root/kernel/lib/app.c" || \
    fail "missing user $name wrapper"
done
grep -q 'TCP_STATE_LISTEN' "$root/kernel/include/timeros/net/tcp.h" || \
  fail 'missing LISTEN state'
grep -q 'TCP_STATE_SYN_RECEIVED' "$root/kernel/include/timeros/net/tcp.h" || \
  fail 'missing SYN_RECEIVED state'
grep -q 'tcp_server_echo' "$root/user/Makefile" || \
  fail 'missing server Echo target'

echo 'PASS: M6C2 passive TCP contracts'
```

- [ ] **Step 2: Run the contract and verify RED**

Run:

```bash
bash tests/host/test_m6c2_contracts.sh
```

Expected: FAIL at `missing m6c2-build target`.

- [ ] **Step 3: Add the stage wrappers and propagation**

Create `scripts/m6c2-build.sh`:

```bash
#!/usr/bin/env bash
set -eu

root=${QS_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
export QS_ROOT=$root
export QS_STAGE=m6c2
export QS_M6C2_TEST=1
export QS_M6C1_TEST=1
export QS_M6B_TEST=1
exec "$root/scripts/m6c1-build.sh"
```

Create `scripts/m6c2-smoke.sh`:

```bash
#!/usr/bin/env bash
set -eu

root=${QS_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
export QS_ROOT=$root
export QS_STAGE=m6c2
export QS_TEST_NAME=m6c2-smoke
exec "$root/scripts/m6c1-smoke.sh"
```

Change M6C1 wrappers to assign their stage and test name only when unset, as
M6B already does. Extend `scripts/m6b-build.sh` with:

```bash
if [ "${QS_M6C2_TEST:-0}" = 1 ]; then
  export QS_KERNEL_CPPFLAGS="$QS_KERNEL_CPPFLAGS -DQS_M6C2_TEST"
fi
```

Add `m6c2-build` and `m6c2-smoke` to `.PHONY` and add:

```make
m6c2-build: check-env check-sources
	./scripts/m6c2-build.sh

m6c2-smoke:
	./scripts/m6c2-smoke.sh
```

Register `test_m6c2_contracts.sh` at the end of `test-host`. Do not add empty
ABI declarations merely to satisfy the contract; Step 4 intentionally remains
red until Tasks 3-5 land.

- [ ] **Step 4: Run wrapper regressions**

Run:

```bash
bash -n scripts/m6c1-build.sh scripts/m6c1-smoke.sh \
  scripts/m6c2-build.sh scripts/m6c2-smoke.sh
bash tests/host/test_m6c1_contracts.sh
bash tests/host/test_m6b_contracts.sh
```

Expected: shell syntax and M6B/M6C1 contracts PASS; M6C2 contract still fails
at the first unimplemented ABI requirement.

- [ ] **Step 5: Commit the scaffold**

```bash
git add Makefile scripts/m6b-build.sh scripts/m6c1-build.sh \
  scripts/m6c1-smoke.sh scripts/m6c2-build.sh scripts/m6c2-smoke.sh \
  tests/host/test_m6c2_contracts.sh
git commit -m "test: define m6c2 passive tcp contracts"
```

### Task 2: Move TCP PCBs Into Stable Pool Storage

**Files:**
- Create: `tests/host/run_m6c2_tcp_test.sh`
- Create: `tests/host/test_m6c2_pool.c`
- Create: `tests/host/test_m6c2_pool.sh`
- Modify: `kernel/include/timeros/net/tcp.h`
- Modify: `kernel/src/net/tcp.c`
- Modify: `kernel/src/net/socket.c`
- Modify: `tests/host/test_m6c1_retrans.c`
- Modify: `tests/host/test_m6c1_socket.c`
- Modify: `Makefile`

- [ ] **Step 1: Write the failing pool test**

The C test initializes the existing host net runtime, allocates exactly
`TCP_PCB_MAX` pointers through the new out-parameter API, proves exhaustion,
closes one closed PCB, runs the one-millisecond release timer, and proves the
same pool address is reusable:

```c
net_err_t ipv4_register_handler(uint8_t protocol,
                                ipv4_input_handler_t handler)
{
    assert(protocol == NET_PROTOCOL_TCP);
    assert(handler == tcp_in);
    return NET_ERR_OK;
}

net_err_t ipv4_out(netif_t *netif, const ipaddr_t *dest, uint8_t protocol,
                   pktbuf_t *buf)
{
    (void)netif;
    (void)dest;
    assert(protocol == NET_PROTOCOL_TCP);
    pktbuf_free(buf);
    return NET_ERR_OK;
}

assert(net_sys_init() == NET_ERR_OK);
assert(pktbuf_init() == NET_ERR_OK);
assert(net_timer_init() == NET_ERR_OK);
assert(tcp_init() == NET_ERR_OK);
tcp_pcb_t *pcbs[TCP_PCB_MAX + 1] = { 0 };
for (int i = 0; i < TCP_PCB_MAX; i++) {
    assert(tcp_open(&pcbs[i]) == NET_ERR_OK);
    assert(pcbs[i] != 0);
    for (int j = 0; j < i; j++)
        assert(pcbs[i] != pcbs[j]);
}
assert(tcp_open(&pcbs[TCP_PCB_MAX]) == NET_ERR_MEM);
tcp_pcb_t *released = pcbs[3];
assert(tcp_close(released) == NET_ERR_OK);
assert(net_timer_check_tmo(1) == NET_ERR_OK);
pcbs[3] = 0;
assert(tcp_open(&pcbs[TCP_PCB_MAX]) == NET_ERR_OK);
assert(pcbs[TCP_PCB_MAX] == released);
for (int i = 0; i <= TCP_PCB_MAX; i++) {
    if (pcbs[i] != 0)
        assert(tcp_close(pcbs[i]) == NET_ERR_OK);
}
assert(net_timer_check_tmo(1) == NET_ERR_OK);
```

Create `tests/host/run_m6c2_tcp_test.sh` so every new TCP test uses the same
sanitized source set:

```bash
#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
source_file=$1
label=$2
extra_flags=${3:-}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-sanitize-recover=all -pthread $extra_flags \
  -I"$root/kernel/include" -I"$root/kernel/include/timeros/net" \
  "$root/$source_file" \
  "$root/tests/host/net_host_port.c" \
  "$root/kernel/src/net/net_sys.c" \
  "$root/kernel/src/net/nlist.c" \
  "$root/kernel/src/net/nlocker.c" \
  "$root/kernel/src/net/mblock.c" \
  "$root/kernel/src/net/fixq.c" \
  "$root/kernel/src/net/pktbuf.c" \
  "$root/kernel/src/net/ipaddr.c" \
  "$root/kernel/src/net/tools.c" \
  "$root/kernel/src/net/timer.c" \
  "$root/kernel/src/net/udp.c" \
  "$root/kernel/src/net/tcp.c" \
  "$root/kernel/src/net/socket.c" \
  -o "$tmp/test"
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1 "$tmp/test"

echo "PASS: $label"
```

Create `tests/host/test_m6c2_pool.sh`:

```bash
#!/usr/bin/env bash
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
exec "$root/tests/host/run_m6c2_tcp_test.sh" \
  tests/host/test_m6c2_pool.c "M6C2 stable TCP PCB pool"
```

Register both files with executable mode, and add the pool test after the M6C1
TCP tests in `test-host`.

- [ ] **Step 2: Run the pool test and verify RED**

Run:

```bash
bash tests/host/test_m6c2_pool.sh
```

Expected: compilation fails because `tcp_open` still accepts a caller-owned
`tcp_pcb_t *` rather than `tcp_pcb_t **`.

- [ ] **Step 3: Define the pooled API**

Change the public declaration to:

```c
net_err_t tcp_open(tcp_pcb_t **result);
```

In `tcp.c`, add stable storage and a lock-held allocator:

```c
static tcp_pcb_t pcb_storage[TCP_PCB_MAX];
static tcp_pcb_t *pcbs[TCP_PCB_MAX];

static net_err_t tcp_alloc_locked(tcp_pcb_t **result)
{
    if (result == 0)
        return NET_ERR_PARAM;
    for (int i = 0; i < TCP_PCB_MAX; i++) {
        if (pcbs[i] != 0)
            continue;
        tcp_pcb_t *pcb = &pcb_storage[i];
        plat_memset(pcb, 0, sizeof(*pcb));
        nlocker_init(&pcb->recv_locker, NLOCKER_THREAD);
        nlocker_init(&pcb->state_locker, NLOCKER_THREAD);
        pcb->connect_done = sys_sem_create(0);
        if (pcb->connect_done == SYS_SEM_INVALID)
            goto fail;
        pcb->recv_done = sys_sem_create(0);
        if (pcb->recv_done == SYS_SEM_INVALID)
            goto fail;
        pcb->close_done = sys_sem_create(0);
        if (pcb->close_done == SYS_SEM_INVALID)
            goto fail;
        pcb->opened = 1;
        pcb->state = TCP_STATE_CLOSED;
        pcb->error = NET_ERR_OK;
        pcbs[i] = pcb;
        *result = pcb;
        return NET_ERR_OK;
fail:
        sys_sem_free(pcb->close_done);
        sys_sem_free(pcb->recv_done);
        sys_sem_free(pcb->connect_done);
        nlocker_destroy(&pcb->recv_locker);
        nlocker_destroy(&pcb->state_locker);
        plat_memset(pcb, 0, sizeof(*pcb));
        *result = 0;
        return NET_ERR_MEM;
    }
    *result = 0;
    return NET_ERR_MEM;
}
```

Public `tcp_open` validates `result`, locks `table_locker`, invokes
`tcp_alloc_locked`, unlocks, and returns its error. `tcp_init` zeroes both
arrays. `tcp_release_now_locked` clears the table slot only after timers,
semaphores, locks, and packet ownership are released.

- [ ] **Step 4: Point TCP socket entries at pooled PCBs**

Change the socket union to:

```c
union {
    udp_pcb_t udp;
    tcp_pcb_t *tcp;
} pcb;
```

TCP open uses `tcp_open(&entry->pcb.tcp)`. Every TCP call passes
`entry->pcb.tcp`, not its address. Socket invalidation clears the TCP pointer.
UDP storage and calls remain unchanged. Hold `socket_locker` while scanning and
claiming a free entry so later accept can use the same allocation invariant.

- [ ] **Step 5: Adapt existing direct TCP tests**

Replace stack PCB objects used as live connections with pointers returned by
`tcp_open`. Keep pure header/checksum tests stack-based. Replace calls such as:

```c
tcp_pcb_t pcb;
assert(tcp_open(&pcb) == NET_ERR_OK);
```

with:

```c
tcp_pcb_t *pcb = 0;
assert(tcp_open(&pcb) == NET_ERR_OK);
assert(pcb != 0);
```

and pass `pcb` to all TCP APIs. Exhaustion tests use a pointer array, not an
array of PCB structs.

- [ ] **Step 6: Run pool and M6C1 regressions**

Run:

```bash
bash tests/host/test_m6c2_pool.sh
bash tests/host/test_m6c1_tcp.sh
bash tests/host/test_m6c1_retrans.sh
bash tests/host/test_m6c1_socket.sh
bash tests/host/test_m6b_socket.sh
```

Expected: all PASS under ASan/UBSan.

- [ ] **Step 7: Commit stable PCB storage**

```bash
git add Makefile kernel/include/timeros/net/tcp.h kernel/src/net/tcp.c \
  kernel/src/net/socket.c tests/host/test_m6c1_retrans.c \
  tests/host/test_m6c1_socket.c tests/host/run_m6c2_tcp_test.sh \
  tests/host/test_m6c2_pool.*
git commit -m "refactor: give tcp pcbs stable pool storage"
```

### Task 3: Implement Passive Handshake And Accept Queue

**Files:**
- Create: `tests/host/test_m6c2_tcp.c`
- Create: `tests/host/test_m6c2_tcp.sh`
- Modify: `kernel/include/timeros/net/tcp.h`
- Modify: `kernel/src/net/tcp.c`
- Modify: `Makefile`

- [ ] **Step 1: Write failing passive TCP tests**

The host test must drive real `tcp_in` packets and assert:

1. `tcp_bind` plus `tcp_listen(4)` enters `LISTEN` and emits nothing.
2. A pure SYN to the bound tuple emits SYN-ACK with ACK `peer_isn + 1`.
3. A wrong final ACK returns `NET_ERR_STATE` and does not make accept ready.
4. The correct ACK creates one established queue head with the exact peer IP
   and port.
5. Four half-open children fill backlog; a fifth SYN returns `NET_ERR_FULL`.
6. SYN-ACK retry exhaustion removes the half-open child and frees its packet
   and PCB slot.
7. Closing the listener releases every unaccepted child after accept waiters
   drain.
8. A queued child that receives FIN before accept is removed and released
   instead of leaving a non-established queue entry.

Use the M6C1 packet construction semantics, with output capture for destination
port, sequence, ACK, flags, and payload length. Create the shell wrapper:

```bash
#!/usr/bin/env bash
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
exec "$root/tests/host/run_m6c2_tcp_test.sh" \
  tests/host/test_m6c2_tcp.c "M6C2 passive TCP state machine"
```

- [ ] **Step 2: Run the passive TCP test and verify RED**

Run:

```bash
bash tests/host/test_m6c2_tcp.sh
```

Expected: compilation fails because listener states and passive APIs are absent.

- [ ] **Step 3: Add states and listener fields**

Add:

```c
#define TCP_ACCEPT_MAX 4

typedef enum _tcp_state_t {
    TCP_STATE_CLOSED,
    TCP_STATE_LISTEN,
    TCP_STATE_SYN_SENT,
    TCP_STATE_SYN_RECEIVED,
    TCP_STATE_ESTABLISHED,
    TCP_STATE_FIN_WAIT_1,
    TCP_STATE_FIN_WAIT_2,
    TCP_STATE_TIME_WAIT,
} tcp_state_t;
```

Forward-declare `tcp_pcb_t` and add these fields to the PCB:

```c
tcp_pcb_t *listener;
tcp_pcb_t *accept_queue[TCP_ACCEPT_MAX];
int accept_head;
int accept_count;
int pending_count;
int backlog;
int accept_waiters;
int passive;
int close_requested;
sys_sem_t accept_done;
```

Add exact APIs:

```c
net_err_t tcp_bind(tcp_pcb_t *pcb, netif_t *netif,
                   const ipaddr_t *local, uint16_t port);
net_err_t tcp_listen(tcp_pcb_t *pcb, int backlog);
net_err_t tcp_accept_acquire(tcp_pcb_t *listener);
net_err_t tcp_accept_peek_acquired(tcp_pcb_t *listener,
                                   tcp_pcb_t **child,
                                   ipaddr_t *remote,
                                   uint16_t *remote_port,
                                   int timeout_ms);
net_err_t tcp_accept_commit_acquired(tcp_pcb_t *listener,
                                     tcp_pcb_t *child);
void tcp_accept_release_acquired(tcp_pcb_t *listener);
```

- [ ] **Step 4: Implement bind, listen, and listener lookup**

`tcp_bind` requires an allocated, closed, unbound PCB, active netif, nonzero
port, and local address equal to wildcard or the netif address. It rejects a
live PCB using the same local address/port. Wildcard is stored as the netif
address for exact input matching.

`tcp_listen` requires backlog `1..TCP_ACCEPT_MAX`, a bound PCB, and no other
live `LISTEN` PCB. It creates `accept_done`, zeros queue counters, and changes
state only after semaphore creation succeeds.

Reject active connect on a bound or listening PCB. Restrict receive waiter
acquisition to connected states so a listener cannot enter the byte-stream
receive path. `tcp_send_start` retains its existing `ESTABLISHED` requirement.

Add a listener lookup that matches `LISTEN`, netif, local address, and local
port. Exact connected four-tuple lookup always runs first.

- [ ] **Step 5: Allocate and advance passive children**

When no exact PCB matches, accept only a header-only packet with flags exactly
`TCP_FLAG_SYN`. Under `table_locker`:

```c
if (listener->pending_count >= listener->backlog)
    TCP_IN_RETURN(NET_ERR_FULL);
tcp_pcb_t *child = 0;
err = tcp_alloc_locked(&child);
if (err < 0)
    TCP_IN_RETURN(err);
child->listener = listener;
child->passive = 1;
child->netif = netif;
ipaddr_copy(&child->local_ip, dest);
ipaddr_copy(&child->remote_ip, src);
child->local_port = dest_port;
child->remote_port = src_port;
child->iss = next_iss;
next_iss += 64000U;
child->snd_una = child->iss;
child->snd_nxt = child->iss + 1U;
child->rcv_nxt = seq + 1U;
child->window = window;
child->state = TCP_STATE_SYN_RECEIVED;
listener->pending_count++;
```

Send SYN-ACK through `tcp_start_outstanding`. If allocation, output, or timer
arming fails, detach and release the child and decrement `pending_count` before
returning.

For `SYN_RECEIVED`, accept only a header-only ACK with sequence `rcv_nxt` and
acknowledgement `iss + 1`. Clear SYN-ACK ownership, enter `ESTABLISHED`, append
the child at `(accept_head + accept_count) % TCP_ACCEPT_MAX`, increment
`accept_count`, and notify `accept_done` once. A duplicate SYN with the original
sequence retransmits the owned SYN-ACK without creating another child.

- [ ] **Step 6: Implement accept pinning and cleanup**

`tcp_accept_acquire` increments `accept_waiters` only on a live listener.
`tcp_accept_peek_acquired` waits until the queue is nonempty or the listener is
terminal, then returns the queue head and peer tuple without removing it.
`tcp_accept_commit_acquired` verifies the same child is still at the head,
removes it, clears `child->listener`, decrements `pending_count` and the waiter,
and returns success. `tcp_accept_release_acquired` decrements the waiter without
touching the queue; if a child remains queued, it notifies `accept_done` so an
already sleeping waiter can retry.

Add `accept_waiters == 0` to every listener release predicate in
`tcp_release_proc` and `tcp_release_now_locked`. Non-listener PCBs keep the
existing connect/recv/close waiter requirements.

When a half-open child's retries reach `TCP_RETRY_MAX`, detach it from the
listener, decrement `pending_count`, and schedule its existing one-millisecond
release. Listener close marks terminal, wakes every accept waiter, and defers
child cleanup and listener release until `accept_waiters == 0`. Keep the
listener state as `LISTEN` while `release_pending` is set so release can retain
its role; accept operations treat `release_pending` as terminal.

If an established, unaccepted child receives FIN or enters a terminal error,
remove its pointer from the four-entry ring while preserving the remaining
order, decrement `accept_count` and `pending_count` exactly once, and schedule
the child release. Accepted children have `listener == 0` and follow the normal
M6C1 close path.

Extend close-wait acquisition so a `LISTEN` PCB with `release_pending` can pin
its existing `close_done` semaphore. Listener release notifies close waiters;
the socket close loop therefore blocks until accept waiters and unaccepted
children are gone, then invalidates the listener handle through the existing
generation path.

- [ ] **Step 7: Run passive and active regressions**

Run:

```bash
bash tests/host/test_m6c2_tcp.sh
bash tests/host/test_m6c2_pool.sh
bash tests/host/test_m6c1_retrans.sh
bash tests/host/test_m6c1_socket.sh
```

Expected: all PASS with zero sanitizer findings.

- [ ] **Step 8: Commit passive TCP core**

```bash
git add Makefile kernel/include/timeros/net/tcp.h kernel/src/net/tcp.c \
  tests/host/test_m6c2_tcp.*
git commit -m "feat: add passive tcp handshake and accept queue"
```

### Task 4: Add Socket Listen And Two-Phase Accept

**Files:**
- Create: `tests/host/test_m6c2_socket.c`
- Create: `tests/host/test_m6c2_socket.sh`
- Modify: `kernel/include/timeros/net/socket.h`
- Modify: `kernel/src/net/socket.c`
- Modify: `kernel/src/syscall.c`
- Modify: `tests/host/test_m6b_socket.c`
- Modify: `tests/host/test_m6c1_socket.c`
- Modify: `Makefile`

- [ ] **Step 1: Write failing socket lifetime tests**

Define a token contract in the test and assert:

- UDP rejects TCP listen and accept operations;
- TCP bind/listen succeeds once and rejects connect, send, and recv on the
  listener;
- prepare blocks until a real passive handshake reaches the accept queue;
- abort leaves the queued child available to the next prepare;
- commit returns a new handle that sends, receives, and closes normally;
- a full socket table makes commit return `NET_ERR_FULL` without dequeue;
- closing the listener wakes a prepared waiter with `NET_ERR_STATE`;
- accepted-child handles change generation after close and reuse.

Use pthread barriers and the existing `SOCKET_TEST` acquired/unlocked hooks to
force close between waiter acquisition and blocking. Create the shell wrapper:

```bash
#!/usr/bin/env bash
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
exec "$root/tests/host/run_m6c2_tcp_test.sh" \
  tests/host/test_m6c2_socket.c "M6C2 passive socket lifetime" \
  -DSOCKET_TEST
```

- [ ] **Step 2: Run the socket test and verify RED**

Run:

```bash
bash tests/host/test_m6c2_socket.sh
```

Expected: compilation fails because socket listen and accept token APIs are absent.

- [ ] **Step 3: Define the socket accept token**

Add:

```c
typedef struct _net_socket_accept_t {
    int listener_handle;
    tcp_pcb_t *listener;
    tcp_pcb_t *child;
    ipaddr_t remote_ip;
    uint16_t remote_port;
    int acquired;
} net_socket_accept_t;

net_err_t net_socket_bind(int handle, netif_t *netif,
                          const ipaddr_t *local, uint16_t port);
net_err_t net_socket_listen(int handle, int backlog);
net_err_t net_socket_accept_prepare(int handle,
                                    net_socket_accept_t *accept);
int net_socket_accept_commit(net_socket_accept_t *accept);
void net_socket_accept_abort(net_socket_accept_t *accept);
```

Update UDP callers to pass the default netif/local address while retaining
their existing `udp_bind` behavior. Update the existing bind worker call to
set `request.address.q_addr = address.address`, then pass
`net_stack_default()` and `&request.address`. Change direct M6B/M6C1 host calls
to `net_socket_bind(handle, 0, 0, port)`; UDP ignores the two new arguments.
This keeps every intermediate commit target-buildable before the listen/accept
syscall task.

- [ ] **Step 4: Implement prepare, commit, and abort**

Prepare locks `socket_locker`, validates a nonclosing TCP listener, calls
`tcp_accept_acquire` before unlocking, sets `acquired`, then waits through
`tcp_accept_peek_acquired`. On any wait error it calls abort.

Commit requires an acquired token, locks `socket_locker`, revalidates the
original handle and listener pointer, finds a nonretired free entry, and calls
`tcp_accept_commit_acquired`. Only after commit succeeds does it set:

```c
entry->used = 1;
entry->closing = 0;
entry->type = NET_SOCKET_TCP;
entry->pcb.tcp = accept->child;
```

It returns the generation-checked handle and clears the token. If no entry is
free, it returns `NET_ERR_FULL` through abort, preserving the queue.

Abort calls `tcp_accept_release_acquired` exactly once and zeros the token. It
is safe after validation, user-copy, listener-close, and socket-full failures.

- [ ] **Step 5: Run socket and lower-layer tests**

Run:

```bash
bash tests/host/test_m6c2_socket.sh
bash tests/host/test_m6c2_tcp.sh
bash tests/host/test_m6c1_socket.sh
bash tests/host/test_m6b_socket.sh
```

Expected: all PASS under ASan/UBSan.

- [ ] **Step 6: Commit socket accept lifetime**

```bash
git add Makefile kernel/include/timeros/net/socket.h kernel/src/net/socket.c \
  kernel/src/syscall.c tests/host/test_m6b_socket.c \
  tests/host/test_m6c1_socket.c tests/host/test_m6c2_socket.*
git commit -m "feat: add bounded tcp listen and accept sockets"
```

### Task 5: Expose Listen And Accept Syscalls

**Files:**
- Create: `tests/host/test_m6c2_syscall.sh`
- Modify: `kernel/include/timeros/syscall.h`
- Modify: `kernel/lib/app.c`
- Modify: `kernel/src/syscall.c`
- Modify: `tests/host/test_m6c2_contracts.sh`
- Modify: `Makefile`

- [ ] **Step 1: Write failing syscall contracts**

Require exact IDs `201` and `202`, dispatch cases, wrappers, listen submission
through `socket_exec`, and this ordering inside `__sys_accept`:

```text
user_range_check(address)
user_range_check(address_length)
net_socket_accept_prepare
copy_to_user(address)
copy_to_user(address_length)
net_socket_accept_commit
```

The awk contract must fail if commit appears before either copy or if prepare
appears before writable-range validation.

- [ ] **Step 2: Run the syscall contract and verify RED**

Run:

```bash
bash tests/host/test_m6c2_syscall.sh
```

Expected: FAIL because `__NR_listen` and `__NR_accept` are absent.

- [ ] **Step 3: Add ABI declarations and wrappers**

Add:

```c
#define __NR_listen 201
#define __NR_accept 202

int sys_listen(int fd, int backlog);
int sys_accept(int fd, net_sockaddr_in *address, size_t *address_length);
```

Implement wrappers:

```c
int sys_listen(int fd, int backlog)
{
    return syscall(__NR_listen, fd, backlog, 0);
}

int sys_accept(int fd, net_sockaddr_in *address, size_t *address_length)
{
    return syscall(__NR_accept, fd, (reg_t)(uintptr_t)address,
                   (reg_t)(uintptr_t)address_length);
}
```

- [ ] **Step 4: Extend worker operations for listen**

Keep the full-address bind request added in Task 4. Add
`SOCKET_EXEC_LISTEN`; it calls
`net_socket_listen(request->handle, request->length)`. Bind and listen remain
short network-worker mutations. `accept` never runs inside `socket_exec`.

- [ ] **Step 5: Implement validation-before-consumption accept**

`__sys_accept` accepts either two null outputs or two valid outputs. For valid
outputs, copy in the length, require it to cover `net_sockaddr_in`, and validate
both writable ranges before prepare. Then:

```c
net_socket_accept_t accept = { 0 };
int result = net_socket_accept_prepare(handle, &accept);
if (result < 0)
    return result;
if (user_address != 0) {
    net_sockaddr_in address = {
        .family = NET_AF_INET,
        .port = x_htons(accept.remote_port),
        .address = accept.remote_ip.q_addr,
    };
    size_t length = sizeof(address);
    if (copy_to_user((char *)user_address, &address, sizeof(address)) < 0 ||
        copy_to_user((char *)user_length, &length, sizeof(length)) < 0) {
        net_socket_accept_abort(&accept);
        return NET_ERR_PARAM;
    }
}
return net_socket_accept_commit(&accept);
```

Dispatch both syscall IDs. Every return after successful prepare must commit or
abort exactly once.

- [ ] **Step 6: Run syscall and socket regressions**

Run:

```bash
bash tests/host/test_m6c2_syscall.sh
bash tests/host/test_m6c2_contracts.sh
bash tests/host/test_m6c2_socket.sh
bash tests/host/test_m6b_syscall.sh
bash tests/host/test_m6c1_contracts.sh
```

Expected: all PASS.

- [ ] **Step 7: Commit the ABI**

```bash
git add Makefile kernel/include/timeros/syscall.h kernel/lib/app.c \
  kernel/src/syscall.c tests/host/test_m6c2_contracts.sh \
  tests/host/test_m6c2_syscall.sh
git commit -m "feat: expose tcp listen and accept syscalls"
```

### Task 6: Defer Close Until Outstanding Echo Is Acknowledged

**Files:**
- Create: `tests/host/test_m6c2_close.c`
- Create: `tests/host/test_m6c2_close.sh`
- Modify: `kernel/include/timeros/net/tcp.h`
- Modify: `kernel/src/net/tcp.c`
- Modify: `Makefile`

- [ ] **Step 1: Write the failing deferred-close test**

Create an established connection, call `tcp_send_start`, then call `tcp_close`
before the data ACK. Assert close returns success without sending FIN. Feed the
data ACK and assert FIN is emitted with sequence equal to the acknowledged data
end. Complete ACK/FIN/TIME-WAIT and verify the PCB returns to the pool. Also
assert retry exhaustion before the data ACK returns the close waiter an error
and does not emit FIN.

Create the shell wrapper:

```bash
#!/usr/bin/env bash
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
exec "$root/tests/host/run_m6c2_tcp_test.sh" \
  tests/host/test_m6c2_close.c "M6C2 deferred TCP close"
```

- [ ] **Step 2: Run the close test and verify RED**

Run:

```bash
bash tests/host/test_m6c2_close.sh
```

Expected: the current implementation returns `NET_ERR_FULL` from close.

- [ ] **Step 3: Implement close-after-ACK**

Use the `close_requested` PCB field. In `tcp_close`, when state is
`ESTABLISHED` and `outstanding != 0`, set `close_requested = 1`, unlock, and
return `NET_ERR_OK`. Socket close therefore reports `NET_ERR_NONE` while the
PCB remains open.

In `tcp_accept_ack`, capture this condition before clearing ownership:

```c
int start_fin = complete && pcb->close_requested &&
                pcb->state == TCP_STATE_ESTABLISHED;
if (start_fin)
    pcb->close_requested = 0;
```

After `tcp_clear_outstanding`, call `tcp_start_local_fin` when `start_fin` is
true and propagate its error through `tcp_fail`. Reset `close_requested` in
every connection reset, failure, and release path.

- [ ] **Step 4: Run close and M6C1 regressions**

Run:

```bash
bash tests/host/test_m6c2_close.sh
bash tests/host/test_m6c2_tcp.sh
bash tests/host/test_m6c1_retrans.sh
bash tests/host/test_m6c1_socket.sh
```

Expected: all PASS.

- [ ] **Step 5: Commit deferred close**

```bash
git add Makefile kernel/include/timeros/net/tcp.h kernel/src/net/tcp.c \
  tests/host/test_m6c2_close.*
git commit -m "fix: defer tcp fin until sent data is acknowledged"
```

### Task 7: Add Guest, Peer, Markers, And Smoke Acceptance

**Files:**
- Create: `user/tcp_server_echo.c`
- Create: `tests/host/test_m6c2_peer.sh`
- Create: `tests/host/test_m6c2_smoke_script.sh`
- Modify: `user/tcp_echo.c`
- Modify: `user/Makefile`
- Modify: `kernel/include/timeros/selftest.h`
- Modify: `kernel/src/selftest.c`
- Modify: `kernel/src/net/tcp.c`
- Modify: `kernel/src/net/socket.c`
- Modify: `scripts/m5-peer.py`
- Modify: `scripts/m5-smoke.sh`
- Modify: `Makefile`

- [ ] **Step 1: Write failing peer and smoke tests**

`test_m6c2_peer.sh` imports `m5-peer.py`, uses a fake AF_PACKET socket, and
feeds the cumulative M5/M6B/M6C1 flows followed by this server-client exchange:

```text
peer SYN -> guest SYN-ACK -> peer ACK
peer payload -> guest ACK -> guest Echo
drop first Echo ACK -> guest retransmitted Echo -> peer ACK
guest FIN -> peer ACK+FIN -> guest ACK
```

It must reject wrong SYN-ACK ACK, wrong Echo sequence, changed retransmission
payload, premature FIN, and wrong final ACK.

`test_m6c2_smoke_script.sh` uses fake peer/QEMU binaries and requires exact-once
ordered M6C2 markers, all cumulative M6C1 and UDP evidence, nonzero server
statistics, zero outstanding records, and failures for missing, duplicate,
late-close, incomplete-stat, peer-exit, QEMU-exit, and guest-fail cases.

- [ ] **Step 2: Run both tests and verify RED**

Run:

```bash
bash tests/host/test_m6c2_peer.sh
bash tests/host/test_m6c2_smoke_script.sh
```

Expected: peer mode, M6C2 markers, and server statistics are absent.

- [ ] **Step 3: Add truthful selftest bits**

The existing completion mask is `u32` and M6C1 already uses bits zero through
25. Use the six remaining mask bits exactly as follows:

```c
#define M6C2_LISTEN_DONE         (1U << 26)
#define M6C2_ACCEPT_DONE         (1U << 27)
#define M6C2_ECHO_DONE           (1U << 28)
#define M6C2_CHILD_CLOSE_DONE    (1U << 29)
#define M6C2_LISTENER_CLOSE_DONE (1U << 30)
#define M6C2_CLOSE_DONE          (1U << 31)
```

Use separate atomic `u32` claim variables for Echo and close marker printing;
do not consume completion-mask bits for claims. Reset both claims in
`m2c_selftest_init`. Expose:

```c
void m6c2_mark_tcp_listen(void);
void m6c2_mark_tcp_accept(void);
void m6c2_mark_tcp_echo(void);
void m6c2_mark_tcp_child_close(void);
void m6c2_mark_tcp_listener_close(void);
```

Listen is marked after `tcp_listen` enters `LISTEN`; accept is marked after a
child receives its socket handle. Echo uses an atomic claim, prints
`QS:M6C2_ECHO_OK`, then sets done only when a passive connection's real
retransmission is acknowledged. Child and listener release set separate bits;
the second cleanup atomically claims, prints `QS:M6C2_CLOSE_OK`, then sets final
close done. `M6C2_ALL_DONE` includes all six M6C2 bits and the cumulative M6C1
mask. `m2c_selftest_poll` requires it before printing
`QS:TEST_PASS:m6c2-smoke`.

Before `tcp_release_now_locked` resets a PCB, capture whether it is the live
listener or an accepted passive child. A passive child counts toward the M6C2
close evidence only when `passive != 0 && listener == 0`; half-open and queued
children still owned by a listener never satisfy the accepted-child cleanup
bit.

- [ ] **Step 4: Add the server program**

Create `tcp_server_echo.c` with fixed payload `quard-star-m6c2` and this syscall
order:

```c
int listener = sys_socket(NET_AF_INET, NET_SOCK_STREAM, 0);
net_sockaddr_in local = {
    .family = NET_AF_INET,
    .port = net_port(4801),
    .address = 0xc0a86402U,
};
if (listener < 0 || sys_bind(listener, &local, sizeof(local)) < 0 ||
    sys_listen(listener, 4) < 0)
    return fail("listen");
printf("QS:M6C2_LISTEN_OK\n");

net_sockaddr_in peer;
size_t peer_length = sizeof(peer);
int client = sys_accept(listener, &peer, &peer_length);
if (client < 0)
    return fail("accept");
printf("QS:M6C2_ACCEPT_OK\n");
```

Receive exactly the fixed payload in a bounded loop, compare it, send the exact
bytes once, then close `client` and `listener`. Echo and close markers come from
truthful kernel completion paths, so the user program does not duplicate them.
Failures print one of the exact markers `QS:TEST_FAIL:m6c2-listen`,
`QS:TEST_FAIL:m6c2-accept`, `QS:TEST_FAIL:m6c2-recv`,
`QS:TEST_FAIL:m6c2-echo`, `QS:TEST_FAIL:m6c2-send`,
`QS:TEST_FAIL:m6c2-client-close`, or `QS:TEST_FAIL:m6c2-listener-close`.

Add `tcp_server_echo` to `user/Makefile`. Under `QS_M6C2_TEST`, successful
M6C1 `tcp_echo` close execs `tcp_server_echo`; earlier stages retain their
current behavior.

- [ ] **Step 5: Extend the TAP peer**

Add `--require-tcp-server` and separate fields:

```python
"tcp_server_syn": 0,
"tcp_server_handshakes": 0,
"tcp_server_data": 0,
"tcp_server_echo": 0,
"tcp_server_retransmissions": 0,
"tcp_server_fin": 0,
"tcp_server_outstanding": 0,
```

After M6C1 client completion, send the server SYN to guest port 4801. Validate
every guest segment's tuple, flags, sequence, ACK, checksum, and payload. Drop
the first Echo ACK, count only a byte-identical retransmission, ACK it, and
complete FIN. `complete()` requires all server counters and outstanding zero
only in M6C2 mode. Existing modes must not initiate the server connection.

- [ ] **Step 6: Extend smoke validation**

Allow `m6c2-smoke`, pass both `--require-tcp` and `--require-tcp-server`, require
the four M6C2 markers plus final pass exactly once, verify their order, and
validate every new peer statistic. Keep M6B UDP and M6C1 active TCP checks
unchanged and cumulative.

- [ ] **Step 7: Run acceptance and regression tests**

Run:

```bash
bash tests/host/test_m6c2_peer.sh
bash tests/host/test_m6c2_smoke_script.sh
bash tests/host/test_m6c2_contracts.sh
bash tests/host/test_m6c1_peer.sh
bash tests/host/test_m6c1_smoke_script.sh
bash tests/host/test_m6b_smoke_script.sh
```

Expected: all PASS.

- [ ] **Step 8: Commit functional acceptance**

```bash
git add Makefile kernel/include/timeros/selftest.h kernel/src/selftest.c \
  kernel/src/net/tcp.c kernel/src/net/socket.c scripts/m5-peer.py \
  scripts/m5-smoke.sh user/Makefile user/tcp_echo.c user/tcp_server_echo.c \
  tests/host/test_m6c2_peer.sh tests/host/test_m6c2_smoke_script.sh
git commit -m "feat: add m6c2 passive tcp echo acceptance"
```

### Task 8: Verify, Review, And Publish The Functional Slice

**Files:**
- No planned changes. A concrete verification failure starts its own failing
  regression and fix commit against the files named by that failure.

- [ ] **Step 1: Run formatting and syntax gates**

Run:

```bash
git diff --check
bash -n scripts/m6c2-build.sh scripts/m6c2-smoke.sh \
  tests/host/test_m6c2_contracts.sh tests/host/test_m6c2_pool.sh \
  tests/host/test_m6c2_tcp.sh tests/host/test_m6c2_socket.sh \
  tests/host/test_m6c2_syscall.sh tests/host/test_m6c2_close.sh \
  tests/host/test_m6c2_peer.sh tests/host/test_m6c2_smoke_script.sh
python3 -c 'import ast, pathlib; ast.parse(pathlib.Path("scripts/m5-peer.py").read_text(encoding="utf-8"))'
```

Expected: all commands exit zero and no `__pycache__` is created.

- [ ] **Step 2: Run the full host suite on Ubuntu 24.04**

Run:

```bash
make test-host
```

Expected: every M0-M6C2 host test exits zero with no ASan/UBSan findings.

- [ ] **Step 3: Build and run real TAP acceptance**

Run:

```bash
make m6c2-build
make m6c2-smoke
```

Expected: firmware builds from clean outputs; QEMU/TAP exits zero with all
cumulative exact-once markers, valid server peer statistics, and zero
outstanding records.

- [ ] **Step 4: Perform strict lifetime review**

Review these invariants against code and tests:

```text
one table entry per allocated PCB
no PCB copying
one listener owner per unaccepted child
accept waiter acquired before socket unlock
copy completed before accept queue commit
half-open timeout decrements backlog exactly once
listener close cannot free a peeked child
accepted child survives listener close
timer removal precedes pool reuse
outstanding packet freed exactly once
final pass cannot precede Echo or close markers
```

Fix each confirmed issue with a failing regression test before changing code.

- [ ] **Step 5: Record environmental limits honestly**

If target build or TAP acceptance cannot run, record the exact failed command
and prerequisite. Do not substitute fake-QEMU smoke, source grep, or host tests
for target evidence.

- [ ] **Step 6: Commit review fixes and push**

When verification finds an issue, first add its failing regression, then stage
only that test and the implementation files changed for the verified fix. Use
commit message `fix: harden m6c2 passive tcp lifetimes`.

Then verify a clean tree and push the branch:

```bash
git status --short
git push -u origin codex/m6c2-passive
```

Do not call the milestone concurrent or stress-complete. The next plan is
`m6c2-stress`, which raises PCB capacity before testing eight simultaneous
connections and 100 reconnects.
