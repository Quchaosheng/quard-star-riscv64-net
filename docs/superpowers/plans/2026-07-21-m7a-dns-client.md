# M7A DNS Client Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a bounded IPv4 DNS `A` resolver over the existing UDP/socket path with host and real QEMU/TAP evidence.

**Architecture:** Keep DNS wire encoding and parsing in a focused `dns.c` module. Use the existing network executor and UDP socket APIs for transport, with no second RPC queue or protocol worker. Add one deterministic guest probe and one local TAP peer mode.

**Tech Stack:** C11 host tests with ASan/UBSan, existing RISC-V C kernel, existing UDP socket ABI, Python TAP peer, Bash smoke scripts.

---

### Task 1: Define The DNS Codec Contract

**Files:**
- Create: `kernel/include/timeros/net/dns.h`
- Create: `kernel/src/net/dns.c`
- Create: `tests/host/test_m7a_dns.c`
- Create: `tests/host/test_m7a_dns.sh`
- Modify: `Makefile`

- [ ] **Step 1: Write the failing host test**

Define tests for query encoding, compressed answer parsing, wrong transaction ID, truncated label, pointer loop, error response, and non-`A` answer. The test calls `dns_query_encode()` and `dns_response_parse()` from `dns.h` and expects the exact address `192.168.100.1` for a valid response.

- [ ] **Step 2: Run the focused test to verify RED**

Run: `bash tests/host/test_m7a_dns.sh`

Expected: compilation fails because `dns.h` and the codec functions do not exist.

- [ ] **Step 3: Add the public types and minimal codec implementation**

Declare:

```c
typedef struct {
    u16 id;
    u8 bytes[512];
    int length;
} dns_query_t;

int dns_query_encode(dns_query_t *query, u16 id, const char *name);
int dns_response_parse(const u8 *packet, int length, u16 id,
                       const char *name, ipaddr_t *address);
```

Implement network-byte-order reads, label encoding, bounded compression-pointer traversal, header/rcode/class/type checks, and exact four-byte `A` extraction. Return existing negative `NET_ERR_*` values; never write `address` until a complete valid answer is found.

- [ ] **Step 4: Run the focused test to verify GREEN**

Run: `bash tests/host/test_m7a_dns.sh`

Expected: `PASS: M7A DNS codec behavior` with no sanitizer output.

- [ ] **Step 5: Commit**

```bash
git add kernel/include/timeros/net/dns.h kernel/src/net/dns.c tests/host/test_m7a_dns.c tests/host/test_m7a_dns.sh Makefile
git commit -m "feat: add bounded dns wire codec"
```

### Task 2: Add The UDP Resolver Operation

**Files:**
- Modify: `kernel/include/timeros/net/dns.h`
- Modify: `kernel/src/net/dns.c`
- Modify: `kernel/src/net/socket.c`
- Modify: `kernel/src/syscall.c`
- Modify: `kernel/include/timeros/syscall.h`
- Modify: `kernel/lib/app.c`
- Create: `tests/host/test_m7a_dns_socket.sh`

- [ ] **Step 1: Write the failing resolver integration test**

Add a fake UDP transport test that verifies destination `192.168.100.1:53`, preserves the query ID, accepts one valid response, rejects a timeout, and rejects a response for a different ID. The test must assert that no partially parsed address is returned on failure.

- [ ] **Step 2: Run it to verify RED**

Run: `bash tests/host/test_m7a_dns_socket.sh`

Expected: FAIL because the resolver entry point is absent.

- [ ] **Step 3: Implement the smallest resolver API**

Add a kernel-side `dns_resolve_a(netif_t *, const ipaddr_t *, const char *, ipaddr_t *, int)` that opens a UDP socket through the existing executor, binds an ephemeral local port, sends the encoded query, receives one datagram with the existing timeout semantics, parses it, and closes the socket on every path. Add a user syscall wrapper only for the M7A probe; do not introduce a general resolver cache.

- [ ] **Step 4: Run focused resolver and existing socket tests**

Run: `bash tests/host/test_m7a_dns_socket.sh` and `make test-host`

Expected: both pass, including all prior M0-M6C2 tests.

- [ ] **Step 5: Commit**

```bash
git add kernel/include/timeros/net/dns.h kernel/src/net/dns.c kernel/src/net/socket.c kernel/src/syscall.c kernel/include/timeros/syscall.h kernel/lib/app.c tests/host/test_m7a_dns_socket.sh
git commit -m "feat: resolve dns a records over udp"
```

### Task 3: Add The Guest Probe And Build Stage

**Files:**
- Create: `user/m7a_dns_echo.c`
- Modify: `user/Makefile`
- Create: `scripts/m7a-build.sh`
- Create: `scripts/m7a-smoke.sh`
- Create: `tests/host/test_m7a_contracts.sh`

- [ ] **Step 1: Write the failing build and marker contracts**

Require the `dns_echo` application, `QS_M7A_DNS_QUERY_OK`, `QS_M7A_DNS_RESOLVE_OK`, `QS_M7A_DNS_TIMEOUT_OK`, `QS:TEST_PASS:m7a-smoke`, isolated `m7a` output, and cumulative M6 build flags.

- [ ] **Step 2: Run the contract test to verify RED**

Run: `bash tests/host/test_m7a_contracts.sh`

Expected: FAIL because the target, build stage, and markers do not exist.

- [ ] **Step 3: Add the bounded guest program and build wrapper**

The guest resolves `m7a.test` at `192.168.100.1`, validates `192.168.100.1`, performs one bounded timeout query against an unused name, prints each marker exactly once, and then yields. `m7a-build.sh` reuses the M6C2 build chain and adds only `QS_M7A_TEST`.

- [ ] **Step 4: Run contracts and compile the image**

Run: `bash tests/host/test_m7a_contracts.sh` and `./scripts/m7a-build.sh`

Expected: both exit zero and produce `out/m7a` artifacts.

- [ ] **Step 5: Commit**

```bash
git add user/dns_echo.c user/Makefile scripts/m7a-build.sh scripts/m7a-smoke.sh tests/host/test_m7a_contracts.sh
git commit -m "feat: add m7a dns guest probe"
```

### Task 4: Add The Deterministic TAP DNS Peer

**Files:**
- Modify: `scripts/m5-peer.py`
- Create: `tests/host/test_m7a_peer.sh`
- Modify: `scripts/m5-smoke.sh`
- Create: `tests/host/test_m7a_smoke_script.sh`

- [ ] **Step 1: Write the failing peer test**

Feed a DNS query into the fake socket and assert the peer validates source/destination MAC, IPv4 tuple, UDP ports, transaction ID, question name, and response address. Add malformed-query and wrong-question rejection cases.

- [ ] **Step 2: Run it to verify RED**

Run: `bash tests/host/test_m7a_peer.sh`

Expected: FAIL because the peer has no DNS mode or counters.

- [ ] **Step 3: Implement one local DNS peer mode**

Add `--require-dns` and counters `dns_queries`, `dns_replies`, and `dns_timeouts`. Return a deterministic compressed `A` response for `m7a.test`; do not contact the public network. Preserve all existing M5/M6 peer modes and exact error behavior.

- [ ] **Step 4: Add smoke marker ordering and peer-stat checks**

Require `QUERY_OK -> RESOLVE_OK -> TIMEOUT_OK -> TEST_PASS`, exact-once markers, zero peer outstanding state, and at least one validated query/reply.

- [ ] **Step 5: Run focused peer and smoke-script tests**

Run: `bash tests/host/test_m7a_peer.sh` and `bash tests/host/test_m7a_smoke_script.sh`

Expected: both print PASS.

- [ ] **Step 6: Commit**

```bash
git add scripts/m5-peer.py tests/host/test_m7a_peer.sh scripts/m5-smoke.sh tests/host/test_m7a_smoke_script.sh
git commit -m "feat: add deterministic m7a dns peer"
```

### Task 5: Full Verification And Real Acceptance

**Files:**
- Modify: `Makefile`
- Modify: `docs/quard-star-riscv64-net-design.md`

- [ ] **Step 1: Register the M7A host tests and target**

Add the focused DNS tests to `test-host`, add `m7a-build` and `m7a-smoke` targets, and document M7A as complete only after real local DNS evidence.

- [ ] **Step 2: Run all static checks**

Run: `make test-host`; `find scripts tests/host -type f -name '*.sh' -print0 | xargs -0 -n1 bash -n`; `python3 -m py_compile scripts/m5-peer.py`; `git diff --check`.

- [ ] **Step 3: Run real local QEMU/TAP acceptance**

Run: `./scripts/m7a-smoke.sh`

Expected: QEMU exits zero, all cumulative M0-M6C2 markers pass, the three M7A markers appear once and in order, and peer stats show a validated query and reply.

- [ ] **Step 4: Commit documentation and target wiring**

```bash
git add Makefile docs/quard-star-riscv64-net-design.md
git commit -m "test: add m7a dns acceptance target"
```

- [ ] **Step 5: Push the feature branch**

```bash
git push -u origin feat/m7a-dns
```
