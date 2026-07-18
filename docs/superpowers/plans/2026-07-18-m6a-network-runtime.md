# M6A Network Runtime Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate blocking network waits, the shared timer, ARP aging/retry, and loopback from the fixed `tiny-tcpip-stack` baseline, then prove them in host tests and real QEMU/TAP.

**Architecture:** A small `net_sys` adapter maps the original stack's semaphore and monotonic-time API onto TimerOS. The baseline delta timer remains single-threaded inside the network worker. IPv4 output returns to the original `netif_out` layering so Ethernet can use ARP while loopback queues packets back into the same core thread.

**Tech Stack:** C11, TimerOS wait queues/semaphores, RISC-V `r_mtime`, pthread host tests, VirtIO-net, QEMU 8.0.2, TAP.

---

## File Map

- `kernel/include/timeros/net/net_sys.h`, `kernel/src/net/net_sys.c`: portable time and semaphore API.
- `kernel/include/timeros/net/timer.h`, `kernel/src/net/timer.c`: baseline delta-ordered software timer.
- `kernel/include/timeros/net/loop.h`, `kernel/src/net/loop.c`: loopback netif driver and instance accessor.
- `kernel/src/net/{fixq,mblock,arp,ether,ipv4,net_stack}.c`: restore baseline behavior at existing ownership boundaries.
- `tests/host/test_m6_*.{c,sh}`: focused runtime, ARP timer, loopback, and script contracts.
- `scripts/m6a-{build,smoke}.sh`, `Makefile`, `kernel/src/selftest.c`: target build and real acceptance markers.

### Task 1: Network Platform Time And Semaphores

**Files:**
- Create: `kernel/include/timeros/net/net_sys.h`
- Create: `kernel/src/net/net_sys.c`
- Create: `tests/host/test_m6_runtime.c`
- Create: `tests/host/test_m6_runtime.sh`
- Modify: `kernel/Makefile`
- Modify: `Makefile`

- [ ] **Step 1: Write the failing platform test**

Create a test that calls `net_sys_init()`, verifies monotonic elapsed time, creates a zero-count semaphore, times out after 20 ms, then wakes a permanent waiter from a pthread:

```c
net_time_t before;
sys_time_curr(&before);
sys_sem_t sem = sys_sem_create(0);
assert(sem != SYS_SEM_INVALID);
assert(sys_sem_wait(sem, 20) == NET_ERR_TMO);
assert(sys_time_goes(&before) >= 10);

pthread_t thread;
assert(pthread_create(&thread, 0, post_sem, sem) == 0);
assert(sys_sem_wait(sem, 0) == NET_ERR_OK);
assert(pthread_join(thread, 0) == 0);
sys_sem_free(sem);
```

- [ ] **Step 2: Verify the test fails**

Run: `bash tests/host/test_m6_runtime.sh`

Expected: compilation fails because `timeros/net/net_sys.h` does not exist.

- [ ] **Step 3: Implement the minimal platform adapter**

Expose the fixed baseline API:

```c
typedef u64 net_time_t;
typedef struct net_sys_sem *sys_sem_t;
#define SYS_SEM_INVALID ((sys_sem_t)0)

net_err_t net_sys_init(void);
void sys_time_curr(net_time_t *time);
int sys_time_goes(net_time_t *previous);
sys_sem_t sys_sem_create(int initial_count);
void sys_sem_free(sys_sem_t sem);
net_err_t sys_sem_wait(sys_sem_t sem, int timeout_ms);
void sys_sem_notify(sys_sem_t sem);
```

On RISC-V, allocate semaphore handles from a bounded static array protected by a spinlock. Map `0` to `sem_wait()` and positive milliseconds to `sem_timedwait(r_mtime() + timeout_ms * 10000ULL)`. On the host, use `pthread_mutex_t`, `pthread_cond_t`, `CLOCK_MONOTONIC`, and `pthread_cond_timedwait`.

- [ ] **Step 4: Run the focused and existing foundation tests**

Run: `bash tests/host/test_m6_runtime.sh && bash tests/host/test_m5_foundation.sh`

Expected: both print `PASS` with ASan/UBSan enabled.

- [ ] **Step 5: Commit**

```bash
git add kernel/include/timeros/net/net_sys.h kernel/src/net/net_sys.c \
  kernel/Makefile Makefile tests/host/test_m6_runtime.c \
  tests/host/test_m6_runtime.sh
git commit -m "feat: add network time and semaphore port"
```

### Task 2: Restore `fixq` And `mblock` Wait Semantics

**Files:**
- Modify: `kernel/include/timeros/net/fixq.h`
- Modify: `kernel/include/timeros/net/mblock.h`
- Modify: `kernel/src/net/fixq.c`
- Modify: `kernel/src/net/mblock.c`
- Modify: `tests/host/test_m6_runtime.c`
- Modify: affected `tests/host/test_m5_*.sh` compile lists

- [ ] **Step 1: Add failing queue and pool cases**

Cover all three timeout modes and ownership:

```c
assert(fixq_send(&queue, &first, -1) == NET_ERR_OK);
assert(fixq_send(&queue, &second, -1) == NET_ERR_FULL);
assert(fixq_send(&queue, &second, 20) == NET_ERR_TMO);
assert(fixq_recv(&queue, -1) == &first);

void *only = mblock_alloc(&blocks, -1);
assert(only != 0);
assert(mblock_alloc(&blocks, -1) == 0);
assert(mblock_alloc(&blocks, 20) == 0);
```

Add pthread cases where a receiver or `mblock_free` wakes a `timeout == 0` waiter.

- [ ] **Step 2: Verify the new assertions fail**

Run: `bash tests/host/test_m6_runtime.sh`

Expected: the timed calls return immediately because the current implementations ignore timeout.

- [ ] **Step 3: Adapt the baseline semaphore model**

Add `send_sem` and `recv_sem` to `fixq_t`, and `alloc_sem` to `mblock_t`. Preserve this operation order:

```c
if (timeout < 0 && queue_is_full)
    return NET_ERR_FULL;
if (sys_sem_wait(queue->send_sem, timeout) < 0)
    return NET_ERR_TMO;
lock_queue_and_insert();
sys_sem_notify(queue->recv_sem);
```

Use the symmetric receive path. Create an allocation semaphore only for `NLOCKER_THREAD`; `NLOCKER_NONE` remains a checked non-blocking pool.

- [ ] **Step 4: Run all users of the changed primitives**

Run: `make test-host`

Expected: all M0-M6 host tests print `PASS`.

- [ ] **Step 5: Commit**

```bash
git add kernel/include/timeros/net/{fixq,mblock}.h \
  kernel/src/net/{fixq,mblock}.c tests/host
git commit -m "feat: restore network queue timeout semantics"
```

### Task 3: Migrate The Shared Delta Timer

**Files:**
- Create: `kernel/include/timeros/net/timer.h`
- Create: `kernel/src/net/timer.c`
- Create: `tests/host/test_m6_timer.c`
- Create: `tests/host/test_m6_timer.sh`
- Modify: `kernel/include/timeros/net/net_cfg.h`
- Modify: `kernel/Makefile`
- Modify: `Makefile`

- [ ] **Step 1: Write failing timer behavior tests**

Register timers at 30 ms and 10 ms, advance by 10 then 20, verify callback order, reload one timer, remove it, and verify it no longer fires:

```c
assert(net_timer_init() == NET_ERR_OK);
assert(net_timer_add(&slow, "slow", record, &slow_count, 30, 0) == NET_ERR_OK);
assert(net_timer_add(&fast, "fast", record, &fast_count, 10,
                     NET_TIMER_RELOAD) == NET_ERR_OK);
assert(net_timer_first_tmo() == 10);
net_timer_check_tmo(10);
assert(fast_count == 1 && slow_count == 0);
net_timer_check_tmo(20);
assert(fast_count == 3 && slow_count == 1);
net_timer_remove(&fast);
```

- [ ] **Step 2: Verify the timer test fails to compile**

Run: `bash tests/host/test_m6_timer.sh`

Expected: `timeros/net/timer.h` is missing.

- [ ] **Step 3: Migrate the baseline timer unchanged in behavior**

Use `nlist_t` as a delta-ordered list. `net_timer_check_tmo()` removes expired timers into a temporary list before callbacks, then reinserts reload timers. Reject null callbacks and non-positive periods with `NET_ERR_PARAM`.

- [ ] **Step 4: Run timer and foundation tests**

Run: `bash tests/host/test_m6_timer.sh && bash tests/host/test_m5_foundation.sh`

Expected: both print `PASS`.

- [ ] **Step 5: Commit**

```bash
git add kernel/include/timeros/net/{timer,net_cfg}.h kernel/src/net/timer.c \
  kernel/Makefile Makefile tests/host/test_m6_timer.*
git commit -m "feat: migrate shared network timer"
```

### Task 4: Restore ARP Aging, Retry, And Cleanup

**Files:**
- Modify: `kernel/include/timeros/net/arp.h`
- Modify: `kernel/include/timeros/net/net_cfg.h`
- Modify: `kernel/src/net/arp.c`
- Create: `tests/host/test_m6_arp_timer.c`
- Create: `tests/host/test_m6_arp_timer.sh`
- Modify: `tests/host/test_m5_arp.sh`

- [ ] **Step 1: Write failing lifecycle tests**

Use one-second test constants. Queue a packet for an unresolved neighbor, advance the timer through every retry, and assert request count plus complete pool recovery. Insert a reply in a second case and assert the waiting packet is transmitted once.

```c
assert(arp_resolve(netif, &missing, pktbuf_alloc(32)) == NET_ERR_OK);
for (int i = 0; i < ARP_ENTRY_RETRY_CNT; i++)
    net_timer_check_tmo(ARP_TIMER_TMO * 1000);
assert(request_count == ARP_ENTRY_RETRY_CNT);
assert_pktbuf_pool_fully_available();
```

- [ ] **Step 2: Verify the lifecycle test fails**

Run: `bash tests/host/test_m6_arp_timer.sh`

Expected: the waiting entry never retries or releases its packet.

- [ ] **Step 3: Adapt the baseline ARP timer callback**

Add `tmo` and `retry` to `arp_entry_t`. Register a reload timer in `arp_init()`. Resolved entries transition to waiting and re-request. Waiting entries reset their interval until retry exhaustion, then call the existing entry cleanup path so queued buffers are freed exactly once.

- [ ] **Step 4: Run ARP, IPv4, and sanitizer tests**

Run: `bash tests/host/test_m5_arp.sh && bash tests/host/test_m6_arp_timer.sh && bash tests/host/test_m5_ipv4_icmp.sh`

Expected: all print `PASS` with no sanitizer diagnostics.

- [ ] **Step 5: Commit**

```bash
git add kernel/include/timeros/net/{arp,net_cfg}.h kernel/src/net/arp.c \
  tests/host/test_m5_arp.sh tests/host/test_m6_arp_timer.*
git commit -m "feat: restore arp cache lifecycle"
```

### Task 5: Migrate Loopback And Restore Netif Layering

**Files:**
- Create: `kernel/include/timeros/net/loop.h`
- Create: `kernel/src/net/loop.c`
- Modify: `kernel/src/net/ether.c`
- Modify: `kernel/src/net/ipv4.c`
- Modify: `kernel/src/net/net_stack.c`
- Modify: `kernel/include/timeros/net/net_stack.h`
- Create: `tests/host/test_m6_loop.c`
- Create: `tests/host/test_m6_loop.sh`
- Modify: `kernel/Makefile`
- Modify: `Makefile`

- [ ] **Step 1: Write the failing loopback ICMP test**

Initialize the stack modules, call `loop_init()`, send an Echo Request to `127.0.0.1`, process two queued packets, and verify the reply plus resource baseline:

```c
netif_t *loop = loop_get_netif();
assert(icmpv4_out_echo(loop, &loop->ipaddr, 0x6d36, 1,
                       "loop", 4) == NET_ERR_OK);
assert(net_stack_process_input(loop) == NET_ERR_OK);
assert(net_stack_process_input(loop) == NET_ERR_OK);
icmpv4_get_stats(&stats);
assert(stats.last_reply_identifier == 0x6d36);
assert_pktbuf_pool_fully_available();
```

The test-only `assert_pktbuf_pool_fully_available()` allocates exactly
`PKTBUF_BUF_CNT` one-byte buffers, verifies the next allocation fails, and
then frees the temporary buffers. It does not add a production inspection API.

- [ ] **Step 2: Verify the test fails**

Run: `bash tests/host/test_m6_loop.sh`

Expected: `loop.h` is missing and IPv4 still routes directly through ARP.

- [ ] **Step 3: Restore the baseline output boundary**

Change the final IPv4 send to:

```c
return netif_out(netif, (ipaddr_t *)dest, buf);
```

Change Ethernet link output to call `arp_resolve(netif, dest, buf)`. Migrate `loop.c` so `xmit` transfers one packet from `out_q` to `in_q`. Add `net_stack_process_input()` to dispatch Ethernet through its link layer and loopback directly to `ipv4_in()`.

- [ ] **Step 4: Run loopback and all existing network tests**

Run: `bash tests/host/test_m6_loop.sh && make test-host`

Expected: loopback and all M0-M6 host tests print `PASS`.

- [ ] **Step 5: Commit**

```bash
git add kernel/include/timeros/net/{loop,net_stack}.h \
  kernel/src/net/{loop,ether,ipv4,net_stack}.c kernel/Makefile Makefile \
  tests/host/test_m6_loop.*
git commit -m "feat: add ipv4 loopback interface"
```

### Task 6: Integrate Timers And M6A Target Markers

**Files:**
- Modify: `kernel/src/net/net_stack.c`
- Modify: `kernel/src/selftest.c`
- Modify: `kernel/include/timeros/selftest.h`
- Modify: `kernel/include/timeros/net/net_cfg.h`
- Create: `scripts/m6a-build.sh`
- Create: `scripts/m6a-smoke.sh`
- Modify: `scripts/m5-smoke.sh`
- Create: `tests/host/test_m6a_contracts.sh`
- Create: `tests/host/test_m6a_smoke_script.sh`
- Modify: `Makefile`

- [ ] **Step 1: Write failing M6A source and smoke contracts**

Require `-DQS_M6A_TEST`, the three M6 markers, timer advancement in the worker, reuse of the M5 peer, and rejection of missing markers or nonzero peer/QEMU exits.

- [ ] **Step 2: Verify both contracts fail**

Run: `bash tests/host/test_m6a_contracts.sh && bash tests/host/test_m6a_smoke_script.sh`

Expected: both fail because M6A targets and markers do not exist.

- [ ] **Step 3: Add the M6A worker probes and scripts**

Initialize `net_sys`, timer, and loopback before opening `eth0`. On each worker iteration, advance timers from `sys_time_goes`, drain loopback packets, run the loop Echo probe, and bound VirtIO receive by `net_timer_first_tmo()`.

Add selftest bits and exact output:

```c
printk("QS:M6_QUEUE_OK\n");
printk("QS:M6_ARP_TIMER_OK\n");
printk("QS:M6_LOOP_OK\n");
printk("QS:TEST_PASS:m6a-smoke\n");
```

`m6a-smoke.sh` reuses the M5 TAP peer and appends M6 markers to the existing acceptance list.

- [ ] **Step 4: Run contracts and the full host suite**

Run: `bash tests/host/test_m6a_contracts.sh && bash tests/host/test_m6a_smoke_script.sh && make test-host`

Expected: every command exits zero and every test prints `PASS`.

- [ ] **Step 5: Commit**

```bash
git add kernel/src/net/net_stack.c kernel/src/selftest.c \
  kernel/include/timeros/{selftest.h,net/net_cfg.h} scripts/m5-smoke.sh \
  scripts/m6a-*.sh tests/host/test_m6a_* Makefile
git commit -m "feat: add m6a runtime acceptance"
```

### Task 7: Real Build, TAP Acceptance, Review, And Integration

**Files:**
- Modify only files required by verified failures.

- [ ] **Step 1: Run clean host verification**

Run: `make test-host && git diff --check`

Expected: all tests pass and `git diff --check` prints nothing.

- [ ] **Step 2: Build in the WSL Linux filesystem**

Run: `make m6a-build`

Expected: custom QEMU, OpenSBI, TimerOS, trusted firmware, and the M6A image build successfully.

- [ ] **Step 3: Run real QEMU/TAP acceptance**

Run: `make m6a-smoke`

Expected: 32 M4 frames, one reset, M5 ARP/Ping, all three M6 markers, `QS:TEST_PASS:m6a-smoke`, and `PASS: m6a-smoke TAP ARP/ICMP acceptance`.

- [ ] **Step 4: Audit ownership and formatting**

Run:

```bash
git diff --check
rg -n "\(void\)timeout" kernel/src/net kernel/include/timeros/net
```

Expected: no ignored timeout parameters, double-free paths, or style diagnostics.

- [ ] **Step 5: Push and fast-forward the default branch after verification**

```bash
git push -u origin codex/m6a-network-runtime
git checkout codex/m0-foundation
git pull --ff-only origin codex/m0-foundation
git merge --ff-only codex/m6a-network-runtime
make test-host
git push origin codex/m0-foundation
```

Expected: the feature and default remote branches point to the same verified M6A commit.
