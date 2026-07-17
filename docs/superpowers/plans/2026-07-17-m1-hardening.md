# M1 Hardening Implementation Plan

**Goal:** Close the remaining single-hart safety and block-I/O gaps before starting SMP work.

**Architecture:** Keep the existing kernel structure. Add source-level host contracts for safety invariants, correct the narrow syscall/process bugs at their current ownership points, and extend the existing QEMU smoke test with one deterministic VirtIO block round trip.

**Tech Stack:** C, RISC-V GCC, Bash, GNU Make, QEMU quard-star, VirtIO MMIO v1.

---

### Task 1: Lock the hardening requirements

**Files:**
- Create: `tests/host/test_m1_kernel_contracts.sh`
- Modify: `tests/host/test_m1_smoke_script.sh`
- Modify: `scripts/m1-smoke.sh`

- [ ] Require distinct `PTE_R` and `PTE_W` user-copy paths.
- [ ] Require register-width syscall dispatch and microsecond conversion.
- [ ] Require trap-frame release during final process cleanup.
- [ ] Require VirtIO MMIO v1 queue registration and `QS:BLOCK_OK`.
- [ ] Run the tests and confirm they fail against the current implementation.

### Task 2: Fix syscall and process lifetime safety

**Files:**
- Modify: `kernel/src/syscall.c`
- Modify: `kernel/include/timeros/syscall.h`
- Modify: `kernel/src/trap.c`
- Modify: `kernel/src/timer.c`
- Modify: `kernel/src/task.c`

- [ ] Reject non-user and out-of-range virtual addresses.
- [ ] Require readable mappings for kernel reads and writable mappings for kernel writes.
- [ ] Preserve 64-bit syscall return values through the trap path.
- [ ] Return actual microseconds from the platform timer.
- [ ] Free the trap-frame physical page only when the process is finally reaped.

### Task 3: Verify real VirtIO block I/O

**Files:**
- Modify: `kernel/src/virtio_disk.c`
- Modify: `kernel/include/timeros/virtio.h`
- Modify: `kernel/src/main.c`

- [ ] Configure queue align and queue PFN for the legacy MMIO transport.
- [ ] Write a deterministic pattern to a disposable disk block.
- [ ] Read the block back and compare every byte.
- [ ] Emit `QS:BLOCK_OK` only after the round trip succeeds.

### Task 4: Verify and publish

- [ ] Run `make check-env`, `make check-sources`, and `make test-host`.
- [ ] Run `tests/host/test_m1_build_contracts.sh`.
- [ ] Run `make m1-build` and `make m1-smoke`.
- [ ] Run `git diff --check`, commit, and push the branch.
