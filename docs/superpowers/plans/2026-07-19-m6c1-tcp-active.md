# M6C1 Active TCP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a bounded, first-party active TCP client that performs a real handshake, reliable one-segment echo with retransmission, and graceful close over the existing network worker.

**Architecture:** Keep all TCP PCB mutation in the existing network worker. Socket syscalls submit short start/stop operations through `net_exec`; blocking `connect`, `recv`, and close completion wait on PCB-owned semaphores while the worker continues polling input and timers. Use one outstanding segment per PCB and a fixed eight-PCB table.

**Tech Stack:** C11 freestanding RISC-V kernel, existing `pktbuf`, IPv4, `net_timer`, `fixq`, `net_exec`, Python raw TAP peer, shell host contracts, and ASan/UBSan host tests.

---

### Task 1: Worktree And Build Contracts

**Files:**
- Modify: `Makefile`
- Create: `scripts/m6c1-build.sh`
- Create: `scripts/m6c1-smoke.sh`
- Create: `tests/host/test_m6c1_contracts.sh`

- [ ] **Step 1: Create the isolated worktree**

Run `git worktree add .worktrees/m6c1-tcp-active -b codex/m6c1-tcp-active codex/m0-foundation`. Keep the existing M6A worktree and M6B branches.

- [ ] **Step 2: Write the failing contract**

The shell test must check `m6c1-build` and `m6c1-smoke` Make targets, `QS_M6C1_TEST` in the build wrapper, reuse of `m6b-build.sh`/`m6b-smoke.sh`, `tcp_echo` in `user/Makefile`, and `NET_PROTOCOL_TCP` in `kernel/src/net/tcp.c`.

Run `bash tests/host/test_m6c1_contracts.sh`; expected result is FAIL because the new targets and files do not yet exist.

- [ ] **Step 3: Implement the minimum wrappers**

`m6c1-build.sh` exports `QS_M6C1_TEST=1` and `QS_M6B_TEST=1`, then invokes the existing M6B build wrapper. `m6c1-smoke.sh` exports `QS_STAGE=m6c1` and `QS_TEST_NAME=m6c1-smoke`, then invokes the existing M5 smoke runner.

- [ ] **Step 4: Verify and commit**

Run `bash tests/host/test_m6c1_contracts.sh` and `git diff --check`; expected result is PASS. Commit with `git add Makefile scripts/m6c1-build.sh scripts/m6c1-smoke.sh tests/host/test_m6c1_contracts.sh && git commit -m "test: define m6c1 tcp build contracts"`.

### Task 2: TCP Header, Checksum, And State Foundation

**Files:**
- Create: `kernel/include/timeros/net/tcp.h`
- Create: `kernel/src/net/tcp.c`
- Modify: `kernel/Makefile`
- Create: `tests/host/test_m6c1_tcp.c`
- Create: `tests/host/test_m6c1_tcp.sh`

- [ ] **Step 1: Write the failing pure tests**

Compile with `-std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined` and assert that `tcp_header_check` rejects a header shorter than 20 bytes or with a data offset below five words; assert that `tcp_state_accept_ack(TCP_STATE_SYN_SENT, 101, 100)` succeeds while an ACK of 100 fails; assert that sequence 100 is in a 32-byte window and sequence 132 is outside it.

Run `bash tests/host/test_m6c1_tcp.sh`; expected result is FAIL because the TCP API is absent.

- [ ] **Step 2: Add the pure TCP definitions**

Define `NET_SOCKET_STREAM 1`, `TCP_HEADER_SIZE 20`, `TCP_PCB_MAX 8`, `TCP_RECV_MAX 2048`, `TCP_MSS 512`, a packed header with ports, sequence, acknowledgement, data offset, flags, window, checksum, and urgent fields, and states `CLOSED`, `SYN_SENT`, `ESTABLISHED`, `FIN_WAIT_1`, `FIN_WAIT_2`, and `TIME_WAIT`. Expose `tcp_header_check`, `tcp_checksum`, `tcp_sequence_in_window`, and `tcp_state_accept_ack`. Check the TCP pseudo-header plus exactly the declared segment length.

- [ ] **Step 3: Verify and commit**

Run `bash tests/host/test_m6c1_tcp.sh`; expected result is PASS. Commit `kernel/include/timeros/net/tcp.h`, `kernel/src/net/tcp.c`, `kernel/Makefile`, and the two tests as `feat: add tcp header and state foundation`.

### Task 3: Active PCB, Handshake, Stream Queue, And Retransmission

**Files:**
- Modify: `kernel/include/timeros/net/tcp.h`
- Modify: `kernel/src/net/tcp.c`
- Modify: `kernel/src/net/net_stack.c`
- Create: `tests/host/test_m6c1_retrans.c`
- Create: `tests/host/test_m6c1_retrans.sh`

- [ ] **Step 1: Write failing retransmission tests**

Cover PCB open/close, `SYN_SENT` after `tcp_connect_start`, timer expiry at 500 ms, retry count one after the first expiry, terminal `NET_ERR_TMO` after five retries, and receive behavior for one exact-sequence segment, one duplicate, and one future segment. Assert that terminal paths free the outstanding packet and wake waiters.

- [ ] **Step 2: Implement the fixed PCB**

Each PCB owns local/remote tuple, state, `iss`, `snd_una`, `snd_nxt`, `rcv_nxt`, window, one outstanding `pktbuf_t`, retry count, `net_timer_t`, fixed receive byte storage, and connect/receive/close semaphores. `tcp_open` rolls all allocations back in reverse order. `tcp_close` removes timers, frees the outstanding packet and queued bytes, wakes waiters, and clears the table slot once.

The public PCB API is fixed to `tcp_init`, `tcp_open`, `tcp_connect_start`,
`tcp_send_start`, `tcp_recv_bytes`, `tcp_retransmit_due`, and `tcp_close`.
`tcp_connect_start` and `tcp_send_start` only initiate work; they never wait
for peer packets. `tcp_recv_bytes` waits on the PCB receive queue, and
`tcp_retransmit_due` is the timer-test entry that advances one expiry.

- [ ] **Step 3: Implement active handshake and output**

`tcp_connect_start` chooses an ephemeral port, sends SYN through `ipv4_out`, arms the 500 ms timer, and returns immediately. `tcp_in` accepts only a matching SYN-ACK, advances sequence state, sends the final ACK, enters `ESTABLISHED`, and notifies `connect_done`. `tcp_send_start` copies at most `TCP_MSS` bytes, sends PSH/ACK, records its sequence span, and returns without sleeping in the worker.

- [ ] **Step 4: Implement input, retry, and close**

Accept only matching tuples, valid checksum/length, and `seq == rcv_nxt`; duplicate data is ACKed and discarded, future data is ACKed without queueing. ACK of the outstanding segment frees it and removes its timer. Timer expiry retransmits the owned packet five times at most, then records a terminal error and wakes all waiters. FIN moves through FIN-WAIT states, sends ACK, and retains the PCB until TIME-WAIT cleanup.

- [ ] **Step 5: Register and verify**

Register protocol 6 after IPv4 initialization, call `tcp_init` from `net_stack_init`, and add `net/tcp.c` to `NET_SRCS_C`. Run `bash tests/host/test_m6c1_tcp.sh && bash tests/host/test_m6c1_retrans.sh`; expected result is PASS. Commit as `feat: add active tcp pcb and retransmission`.

### Task 4: TCP Socket Handles And Syscalls

**Files:**
- Modify: `kernel/include/timeros/net/socket.h`
- Modify: `kernel/src/net/socket.c`
- Modify: `kernel/include/timeros/syscall.h`
- Modify: `kernel/src/syscall.c`
- Modify: `kernel/lib/app.c`
- Create: `tests/host/test_m6c1_socket.c`
- Create: `tests/host/test_m6c1_socket.sh`
- Modify: `tests/host/test_m6c1_contracts.sh`

- [ ] **Step 1: Write failing socket tests**

Open a `NET_SOCKET_TCP` handle, reject it through UDP-only operations, start connect, send a payload, close it, and assert the stale handle returns `NET_ERR_PARAM`. The shell contract must require `__NR_connect`, `__NR_send`, `__NR_recv`, the three user wrappers, TCP type checking, and writable-range validation before receive consumption.

- [ ] **Step 2: Extend the socket entry**

Change entries to a type-tagged union of `udp_pcb_t` and `tcp_pcb_t`. Preserve M6B generation, `closing`, `retired`, and lifetime-lock behavior. TCP operations reject UDP handles; UDP behavior and tests remain unchanged. `connect`/`send` submit short worker callbacks; `recv` acquires the PCB waiter before blocking.

- [ ] **Step 3: Add the ABI**

Use custom non-conflicting IDs `__NR_connect 203`, `__NR_send 208`, and `__NR_recv 209`. Add fixed-width wrappers to `syscall.h` and `app.c`. `connect` validates the address before submission; `send` copies at most `TCP_MSS` bytes; `recv` validates all user writable ranges before waiting and preserves the stream on failed copy. Blocking waits happen after executor callbacks return.

- [ ] **Step 4: Verify and commit**

Run `bash tests/host/test_m6c1_socket.sh && bash tests/host/test_m6c1_contracts.sh`; expected result is PASS. Commit as `feat: expose active tcp socket syscalls`.

### Task 5: Truthful Selftest, User Echo, And TAP Peer

**Files:**
- Modify: `kernel/include/timeros/selftest.h`
- Modify: `kernel/src/selftest.c`
- Create: `user/tcp_echo.c`
- Modify: `user/Makefile`
- Modify: `scripts/m5-peer.py`
- Modify: `scripts/m5-smoke.sh`
- Modify: `scripts/m6c1-build.sh`
- Modify: `scripts/m6c1-smoke.sh`
- Create: `tests/host/test_m6c1_smoke_script.sh`

- [ ] **Step 1: Write failing acceptance contracts**

Require exact markers `QS:M6C1_TCP_OK`, `QS:M6C1_TCP_RETRANS_OK`, `QS:M6C1_TCP_CLOSE_OK`, and `QS:TEST_PASS:m6c1-smoke`; require peer JSON keys `tcp_syn`, `tcp_data`, `tcp_retransmissions`, `tcp_fin`, and `tcp_outstanding`. The fake peer test must fail on missing or duplicate markers and zero required TCP statistics.

- [ ] **Step 2: Add selftest bits**

Add three `QS_M6C1_TEST` bits. Set handshake only after `ESTABLISHED`, retransmission only after an acknowledged retransmission, and close only after terminal PCB cleanup. Print the final marker only when these bits and all earlier stage bits are set.

- [ ] **Step 3: Add `user/tcp_echo.c`**

Open a stream socket, connect to `192.168.100.1:4800`, send a payload shorter than `TCP_MSS`, receive and compare the exact bytes, print the three markers once, and close. Every failed syscall prints a `QS:TEST_FAIL:m6c1-*` marker and exits nonzero.

- [ ] **Step 4: Extend `m5-peer.py`**

Add TCP encode/decode and pseudo-header checksum helpers. Track one guest tuple and sequence/ack state; answer SYN, echo payload, deliberately drop the first data ACK, acknowledge the retransmission, and complete FIN. Record SYN, data, retransmission, FIN, and outstanding counts without changing existing M4-M6B behavior.

- [ ] **Step 5: Verify and commit**

Run `bash tests/host/test_m6c1_smoke_script.sh && bash tests/host/test_m6c1_contracts.sh`; expected result is PASS. Commit as `feat: add m6c1 tcp echo acceptance`.

### Task 6: Full Verification And Push

**Files:**
- Modify: `docs/source-migration.md` only if the final module mapping needs a new entry
- Modify: `README.md` only if the milestone table lacks M6C1 status

- [ ] **Step 1: Run host regression**

From the Ubuntu validation clone run `make test-host`; expected result is exit code 0 with every M0-M6C1 test passing.

- [ ] **Step 2: Build the target**

Run `make m6c1-build`; expected result is `out/m6c1/fw/fw.bin` with no compiler or linker errors.

- [ ] **Step 3: Run real QEMU/TAP acceptance**

Run `make m6c1-smoke`; expected result is exit code 0, all earlier markers, exactly one each of the three M6C1 markers and `QS:TEST_PASS:m6c1-smoke`, peer TCP SYN/data/retransmission/FIN evidence, and `tcp_outstanding == 0`.

- [ ] **Step 4: Review and push**

Review checksum/length validation, sequence arithmetic, packet ownership, timer cancellation, close wakeups, handle generation, and failed user-copy preservation. Re-run focused tests and smoke after every Important finding. Then run `git diff --check`, `git status --short`, and push `codex/m6c1-tcp-active`; preserve existing branches and worktrees.
