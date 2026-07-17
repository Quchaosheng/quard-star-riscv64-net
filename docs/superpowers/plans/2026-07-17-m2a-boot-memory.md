# M2A Boot and Memory Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Start hart 1 through OpenSBI HSM and prove that hart 0/1 can safely share the physical page allocator.

**Architecture:** Keep the migrated kernel layout. Add separate M2 DTBs and build outputs, pass OpenSBI boot arguments into the existing kernel, use libfdt to discover CPUs, and protect a free-page list with the first SMP spinlock.

**Tech Stack:** C, RISC-V assembly, GNU Make, libfdt, OpenSBI HSM, QEMU quard-star.

---

### Task 1: Lock M2A contracts

**Files:**
- Create: `tests/host/test_m2a_contracts.sh`
- Create: `tests/host/test_m2a_smoke_script.sh`
- Modify: `Makefile`

- [ ] Add source contracts requiring M2 DTBs with `cpu@1`, kernel entry arguments, per-CPU storage, SBI HSM start, release/acquire online state, a spinlock-protected free list, and `QS:SMP_ALLOC_OK`.
- [ ] Add a fake-QEMU smoke test requiring `-smp 2`, `QS:HART_ONLINE:0`, `QS:HART_ONLINE:1`, `QS:SMP_ALLOC_OK`, and `QS:TEST_PASS:m2a-smoke`.
- [ ] Register both scripts under `make test-host`.
- [ ] Run `make test-host` and confirm only the new M2A requirements fail.

### Task 2: Add dual-hart platform inputs

**Files:**
- Create: `platform/quard-star/dts/quard_star_sbi_m2.dts`
- Create: `platform/quard-star/dts/quard_star_kernel_m2.dts`
- Modify: `platform/quard-star/boot/start.s`
- Modify: `tests/host/test_m2a_contracts.sh`

- [ ] Copy the existing M1 DTBs as the first-party baseline and add `cpu@1`, `cpu1_intc`, hart 1 PLIC/CLINT interrupt entries, and `<&cpu0 &cpu1>` to the untrusted domain.
- [ ] Replace the low-level fixed delay with an SRAM release/acquire flag. Hart 0 copies firmware and publishes readiness; other harts wait with an acquire load before entering OpenSBI.

```asm
li t0, 0x8000
li t1, 1
amoswap.w.rl zero, t1, (t0)

1:
lr.w.aq t1, (t0)
beqz t1, 1b
```

- [ ] Compile both M2 DTBs with `dtc` and verify there are no diagnostics.

### Task 3: Pass boot identity and discover CPUs

**Files:**
- Create: `kernel/include/timeros/cpu.h`
- Create: `kernel/src/cpu.c`
- Modify: `kernel/src/entry.S`
- Modify: `kernel/src/main.c`
- Modify: `kernel/include/timeros/os.h`
- Modify: `kernel/Makefile`

- [ ] Reserve independent 16 KiB boot stacks for the maximum seven normal harts and preserve OpenSBI `a0`/`a1` when entering C.

```c
#define MAX_CPUS 7

struct cpu {
    u64 hartid;
    u32 present;
    u32 started;
    u32 online;
    int noff;
    int intena;
};
```

- [ ] Link the read-only libfdt sources from `third_party/dtc/libfdt` without copying or modifying the submodule.
- [ ] Parse `/cpus`, accept enabled hart IDs below `MAX_CPUS`, and reject an invalid DTB, duplicate hart ID, empty CPU set, or missing boot hart.
- [ ] Bind the current CPU pointer to `tp` before using locks or per-CPU state.
- [ ] Run the host contracts and kernel build; expected result is that only HSM, allocator, and M2 smoke requirements remain red.

### Task 4: Start hart 1 through SBI HSM

**Files:**
- Modify: `kernel/include/timeros/sbi.h`
- Modify: `kernel/src/sbi.c`
- Modify: `kernel/src/cpu.c`
- Modify: `kernel/src/main.c`

- [ ] Add minimal wrappers with register-width arguments and SBI error propagation.

```c
struct sbiret sbi_hart_start(u64 hartid, u64 start_addr, u64 opaque);
struct sbiret sbi_hart_get_status(u64 hartid);
```

- [ ] Hart 0 starts each discovered secondary at `_start`, passing the kernel DTB as `opaque`.
- [ ] The secondary switches to the shared kernel page table, installs its trap entry, then publishes `online` with `__ATOMIC_RELEASE`.
- [ ] Hart 0 waits with `__ATOMIC_ACQUIRE` and a timer-based failure deadline, then emits `QS:HART_ONLINE:<id>`.
- [ ] Build an M2 firmware and confirm OpenSBI reports two platform harts and the kernel reports hart 0/1 online.

### Task 5: Make page allocation SMP-safe

**Files:**
- Create: `kernel/include/timeros/spinlock.h`
- Create: `kernel/src/spinlock.c`
- Modify: `kernel/src/address.c`
- Modify: `kernel/src/printk.c`
- Modify: `kernel/Makefile`

- [ ] Implement a GCC-atomic spinlock with acquire/release semantics and per-CPU interrupt nesting.

```c
struct spinlock {
    u32 locked;
    struct cpu *owner;
};
```

- [ ] Replace `Stack recycled` with an intrusive free-page list stored inside free pages. Protect list access and the free-page counter with one allocator lock.
- [ ] Clear allocated pages after releasing the allocator lock.
- [ ] Serialize the existing printk buffer so two harts cannot corrupt log output.
- [ ] Run a concurrent worker on hart 0 and hart 1. Each worker repeatedly allocates, writes, verifies, and frees a page; compare the final free-page count with the baseline.
- [ ] Emit `QS:SMP_ALLOC_OK` only after both workers finish without corruption or leakage.

### Task 6: Add reproducible M2A build and smoke targets

**Files:**
- Create: `scripts/m2a-build.sh`
- Create: `scripts/m2a-smoke.sh`
- Modify: `scripts/m1-build.sh`
- Modify: `Makefile`

- [ ] Parameterize the existing M1 build script with output stage and DTB paths; keep M1 defaults unchanged.
- [ ] Make `m2a-build.sh` select the M2 DTBs and write only to `out/m2a/`.
- [ ] Make `m2a-smoke.sh` start `-smp 2`, reject `QS:TEST_FAIL`, and require all M2A markers.
- [ ] Preserve the existing VirtIO block round trip on hart 0 and emit `QS:TEST_PASS:m2a-smoke` after the allocator test.

### Task 7: Verify and publish M2A

- [ ] Run `make check-env check-sources test-host`.
- [ ] Run `tests/host/test_m1_build_contracts.sh`.
- [ ] Run `make m1-build m1-smoke`.
- [ ] Run `make m2a-build m2a-smoke`.
- [ ] Run `git diff --check` and inspect the complete staged diff.
- [ ] Commit as `feat: add dual-hart m2a foundation` and push `codex/m2-smp`.
