# M6B Socket And UDP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a serialized network request executor, UDP PCB operations, and a minimal socket syscall path with host and QEMU/TAP evidence.

**Architecture:** Keep UDP protocol state in the existing network worker. Synchronous callers submit operations through one bounded executor queue and wait on a result semaphore. A bounded socket table owns UDP PCB handles until close; no second TCP/IP stack or request queue is introduced.

**Tech Stack:** C11, existing TimerOS semaphores/wait queues, current packet buffers/IPv4/ARP/loopback, host pthread tests, RISC-V QEMU/TAP smoke.

---

### Task 1: Define UDP Wire And Host Contracts

**Files:**
- Create: `kernel/include/timeros/net/udp.h`
- Create: `kernel/src/net/udp.c`
- Create: `tests/host/test_m6b_udp.c`
- Create: `tests/host/test_m6b_udp.sh`
- Modify: `kernel/Makefile`
- Modify: `Makefile`

- [ ] **Step 1: Write failing UDP parser and lifecycle assertions**

Cover valid header construction, truncated headers, length smaller than header, checksum failure, duplicate bind, receive queue full, and close releasing queued packet buffers.

- [ ] **Step 2: Run the focused test and confirm failure**

Run: `bash tests/host/test_m6b_udp.sh`

Expected: compilation fails because `timeros/net/udp.h` and the UDP implementation do not exist.

- [ ] **Step 3: Implement the minimum UDP core**

Add fixed-size PCB storage with local port, receive queue, source tuple, and closed state. Implement header encode/decode and input validation using current checksum and packet-buffer helpers. On malformed input or queue exhaustion free the packet exactly once.

- [ ] **Step 4: Run focused UDP tests and existing network tests**

Run: `bash tests/host/test_m6b_udp.sh && make test-host`

Expected: focused UDP tests and all existing M0-M6A tests print `PASS`.

- [ ] **Step 5: Commit the UDP core**

```bash
git add kernel/include/timeros/net/udp.h kernel/src/net/udp.c \
  tests/host/test_m6b_udp.* kernel/Makefile Makefile
git commit -m "feat: add serialized udp core"
```

### Task 2: Add The Network Request Executor

**Files:**
- Create: `kernel/include/timeros/net/net_exec.h`
- Create: `kernel/src/net/net_exec.c`
- Create: `tests/host/test_m6b_exec.c`
- Create: `tests/host/test_m6b_exec.sh`
- Modify: `kernel/src/net/net_stack.c`
- Modify: `kernel/Makefile`

- [ ] **Step 1: Write failing serialization and timeout tests**

Submit two concurrent callbacks, assert callbacks execute serially in the worker context, verify a bounded queue returns full, and verify a timed result wait returns `NET_ERR_TMO` without freeing the request twice.

- [ ] **Step 2: Run the executor test and confirm failure**

Run: `bash tests/host/test_m6b_exec.sh`

Expected: compilation fails because `net_exec` does not exist.

- [ ] **Step 3: Implement one bounded executor queue**

Use existing `fixq`, `sys_sem_create`, and `sys_sem_wait`. The submitter owns the request until enqueue succeeds; the worker owns it while executing; completion signals exactly once. Integrate one drain call into `net_stack_worker` before VirtIO polling.

- [ ] **Step 4: Run executor, UDP, and regression tests**

Run: `bash tests/host/test_m6b_exec.sh && bash tests/host/test_m6b_udp.sh && make test-host`

Expected: all commands exit zero.

- [ ] **Step 5: Commit the executor**

```bash
git add kernel/include/timeros/net/net_exec.h kernel/src/net/net_exec.c \
  tests/host/test_m6b_exec.* kernel/src/net/net_stack.c kernel/Makefile
git commit -m "feat: add network request executor"
```

### Task 3: Add Bounded Socket Handles

**Files:**
- Create: `kernel/include/timeros/net/socket.h`
- Create: `kernel/src/net/socket.c`
- Create: `tests/host/test_m6b_socket.c`
- Create: `tests/host/test_m6b_socket.sh`
- Modify: `kernel/Makefile`
- Modify: `Makefile`

- [ ] **Step 1: Write failing handle and close tests**

Assert socket allocation returns nonnegative handles, invalid handles fail, duplicate bind fails, close wakes a blocked receive, and all PCB/packet counters return to their initial values.

- [ ] **Step 2: Run the socket test and confirm failure**

Run: `bash tests/host/test_m6b_socket.sh`

Expected: compilation fails because the socket table does not exist.

- [ ] **Step 3: Implement the minimum socket table**

Use a fixed kernel table with generation-checked integer handles. Store only UDP sockets in M6B. Close marks the object closed under its lock, wakes its waiter, calls UDP cleanup through the executor, and releases the slot only after queued packets are freed.

- [ ] **Step 4: Run socket and full host tests**

Run: `bash tests/host/test_m6b_socket.sh && make test-host`

Expected: all tests pass with ASan/UBSan enabled.

- [ ] **Step 5: Commit socket handles**

```bash
git add kernel/include/timeros/net/socket.h kernel/src/net/socket.c \
  tests/host/test_m6b_socket.* kernel/Makefile Makefile
git commit -m "feat: add bounded udp socket handles"
```

### Task 4: Wire Minimal Socket Syscalls

**Files:**
- Modify: `kernel/include/timeros/syscall.h`
- Modify: `kernel/src/syscall.c`
- Modify: `user/include/timeros/syscall.h`
- Create: `tests/host/test_m6b_syscall.c`
- Create: `tests/host/test_m6b_syscall.sh`
- Modify: `tests/host/test_m6a_contracts.sh`

- [ ] **Step 1: Write failing syscall ABI tests**

Cover family/type/protocol validation, fixed-width address length checks, invalid handles, user-copy failures, and negative error returns for unsupported flags.

- [ ] **Step 2: Run the syscall test and confirm failure**

Run: `bash tests/host/test_m6b_syscall.sh`

Expected: compilation fails because socket syscall numbers and dispatch cases are absent.

- [ ] **Step 3: Add five syscall cases**

Define fixed syscall numbers after the existing network-independent range and dispatch `socket`, `bind`, `sendto`, `recvfrom`, and `close` through the socket layer. Reuse `copy_from_user`, `copy_to_user`, and explicit maximum address lengths; do not duplicate the syscall entry path.

- [ ] **Step 4: Run syscall, socket, UDP, and full host tests**

Run: `bash tests/host/test_m6b_syscall.sh && bash tests/host/test_m6b_socket.sh && bash tests/host/test_m6b_udp.sh && make test-host`

Expected: all commands exit zero.

- [ ] **Step 5: Commit the syscall ABI**

```bash
git add kernel/include/timeros/syscall.h kernel/src/syscall.c \
  user/include/timeros/syscall.h tests/host/test_m6b_syscall.* \
  tests/host/test_m6a_contracts.sh
git commit -m "feat: expose minimal udp socket syscalls"
```

### Task 5: Add UDP Echo Target Acceptance

**Files:**
- Create: `scripts/m6b-build.sh`
- Create: `scripts/m6b-smoke.sh`
- Modify: `scripts/m5-smoke.sh`
- Modify: `kernel/src/selftest.c`
- Modify: `kernel/include/timeros/selftest.h`
- Create: `tests/host/test_m6b_contracts.sh`
- Create: `tests/host/test_m6b_smoke_script.sh`
- Modify: `Makefile`

- [ ] **Step 1: Write failing target contracts**

Require M6B build flags, executor integration, UDP markers, exact-once marker validation, peer/QEMU exit handling, and packet/socket cleanup markers.

- [ ] **Step 2: Run target contracts and confirm failure**

Run: `bash tests/host/test_m6b_contracts.sh && bash tests/host/test_m6b_smoke_script.sh`

Expected: both fail because M6B target scripts and markers do not exist.

- [ ] **Step 3: Add the M6B build and smoke scripts**

Reuse M6A build and TAP peer setup. Add a UDP peer mode that sends one datagram, validates the echoed payload/source tuple, then waits for the guest timeout marker. The guest emits exactly `QS:M6B_UDP_OK`, `QS:M6B_UDP_TIMEOUT_OK`, and `QS:TEST_PASS:m6b-smoke`.

- [ ] **Step 4: Run target contracts, host tests, and real smoke**

Run: `bash tests/host/test_m6b_contracts.sh && bash tests/host/test_m6b_smoke_script.sh && make test-host && make m6b-build && make m6b-smoke`

Expected: every command exits zero and the final output contains `PASS: m6b-smoke TAP UDP acceptance`.

- [ ] **Step 5: Commit M6B acceptance**

```bash
git add scripts/m6b-build.sh scripts/m6b-smoke.sh scripts/m5-smoke.sh \
  kernel/src/selftest.c kernel/include/timeros/selftest.h \
  tests/host/test_m6b_contracts.sh tests/host/test_m6b_smoke_script.sh Makefile
git commit -m "feat: add m6b udp smoke acceptance"
```

### Task 6: Review And Integration

**Files:**
- Modify only files required by verified review findings.

- [ ] **Step 1: Run final host and build verification**

Run: `make test-host && make m6b-build && make m6b-smoke && git diff --check`

Expected: all host tests, the M6B image build, and real UDP TAP acceptance pass.

- [ ] **Step 2: Perform full code review**

Review request serialization, packet ownership, timeout conversion, user-copy boundaries, handle generation, UDP checksum/length validation, and exact marker counting. Fix Critical/Important findings and re-run affected tests.

- [ ] **Step 3: Fast-forward and push the default branch**

```bash
git push -u origin codex/m6b-socket-udp
git checkout codex/m0-foundation
git merge --ff-only codex/m6b-socket-udp
make test-host
git push origin codex/m0-foundation
```
