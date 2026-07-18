# M2C Synchronization, Shootdown, and Stress Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete M2 with sleep-based synchronization, SBI IPI/RFENCE, deterministic guest-driven QEMU exit, and a 120-second dual-hart stress target.

**Architecture:** Keep the single locked task table. Add table-backed wait channels and build semaphores/sleeping locks on them; wake idle schedulers with SBI IPI. Because all address spaces use ASID 0, broadcast a full local/remote TLB fence before freeing user page tables.

**Tech Stack:** C, RISC-V assembly, GCC atomics, OpenSBI IPI/RFENCE, QEMU SiFive test device, GNU Make, Bash.

---

### Task 1: Lock M2C Contracts

**Files:**
- Create: `tests/host/test_m2c_contracts.sh`
- Create: `tests/host/test_m2c_smoke_script.sh`
- Modify: `Makefile`

- [ ] **Step 1: Add failing source contracts**

Require `Sleeping`, wait channel/deadline/result fields, wait queue, counting
semaphore, sleeping lock, SBI IPI/RFENCE wrappers, software-interrupt handling,
QEMU test-device creation, local/remote shootdown ordering, and M2C scripts.

- [ ] **Step 2: Add a failing fake-QEMU smoke test**

The fake QEMU must exit with status 0 and emit:

```text
QS:WAIT_OK
QS:IPI_OK
QS:RFENCE_OK
QS:SMP_SCHED_OK
QS:TEST_PASS:m2c-smoke
```

The host test must reject a nonzero QEMU exit even if all markers are present.

- [ ] **Step 3: Verify RED**

Run: `make test-host`

Expected: existing tests pass and M2C tests fail only on missing M2C features.

### Task 2: Add Wait Queues, Semaphores, and Sleeping Locks

**Files:**
- Create: `kernel/include/timeros/wait.h`
- Create: `kernel/src/wait.c`
- Modify: `kernel/include/timeros/task.h`
- Modify: `kernel/include/timeros/os.h`
- Modify: `kernel/src/task.c`
- Modify: `kernel/Makefile`

- [ ] **Step 1: Define synchronization types**

```c
struct wait_queue { u32 waiters; };
struct semaphore {
    struct spinlock lock;
    int count;
    struct wait_queue wait;
};
struct sleeplock {
    struct spinlock lock;
    int locked;
    int owner_pid;
    struct wait_queue wait;
};
```

Expose `wait_queue_init`, `sem_init`, `sem_wait`, `sem_timedwait`, `sem_post`,
`sleeplock_init`, `sleeplock_acquire`, and `sleeplock_release`.

- [ ] **Step 2: Add task sleep metadata**

Add `Sleeping` plus:

```c
void *wait_channel;
u64 wait_deadline;
int wait_result;
struct semaphore child_exit;
```

Initialize these fields whenever a task slot is created or reused.

- [ ] **Step 3: Implement atomic sleep/wakeup**

`task_sleep(channel, caller_lock, deadline)` acquires `task_lock` before
releasing `caller_lock`, marks the current task sleeping, and switches to the
current CPU scheduler. On wake it releases `task_lock`, reacquires
`caller_lock`, and returns `0` or timeout `-1`.

`task_wake(channel, wake_all)` scans sleeping tasks under `task_lock`, marks
matching tasks `Ready`, and returns the number woken.

- [ ] **Step 4: Wake expired tasks in the scheduler**

Before selecting a runnable task, compare absolute deadlines with `r_mtime()`.
Expired tasks become `Ready` with `wait_result = -1`. This must also run when
all tasks were sleeping and a local timer woke the idle scheduler.

- [ ] **Step 5: Build semaphore and sleeping-lock operations**

Semaphore wait loops while count is zero and sleeps on its wait queue while
atomically dropping the semaphore spinlock. Post increments count and wakes one
waiter. Sleeping locks use the same pattern and record the owning PID.

- [ ] **Step 6: Verify GREEN for synchronization contracts**

Run: `tests/host/test_m2c_contracts.sh && make m2b-build`

Expected: wait primitive requirements pass and the kernel still links.

### Task 3: Add SBI IPI and Make Process Wait Sleep

**Files:**
- Modify: `kernel/include/timeros/sbi.h`
- Modify: `kernel/src/sbi.c`
- Modify: `kernel/include/timeros/riscv.h`
- Modify: `kernel/src/timer.c`
- Modify: `kernel/src/trap.c`
- Modify: `kernel/src/task.c`
- Modify: `user/initproc.c`

- [ ] **Step 1: Add the SBI v0.2 IPI wrapper**

```c
struct sbiret sbi_send_ipi(u64 hart_mask, u64 hart_mask_base);
```

Use extension `SBI_EXT_IPI`, function 0, and propagate errors to callers.

- [ ] **Step 2: Handle supervisor software interrupts**

Enable `SIE_SSIE` with each CPU timer/scheduler initialization. Add `SIP_SSIP`
and a helper that clears it. Trap cause 1 clears SSIP, sets `need_resched`, and
records that a real scheduler IPI arrived.

- [ ] **Step 3: Kick idle online harts after wakeup**

While task state is protected, construct a mask of other online idle harts.
After publishing a runnable task, call `sbi_send_ipi(mask, 0)` and panic on an
SBI error. Emit `QS:IPI_OK` only from the receiving software-interrupt path.

- [ ] **Step 4: Replace polling `wait()` with the child semaphore**

When no child is zombie, parent `wait()` blocks on `child_exit`. Child exit
increments the parent semaphore and wakes it before switching permanently to
the scheduler. Preserve the rule that a zombie cannot be reaped until its
kernel context has switched away.

- [ ] **Step 5: Add an M2C-only fork/wait exercise**

Under `QS_M2C_TEST`, init forks one child. The child yields until at least 100 ms
has elapsed and exits; the parent waits, validates the returned PID, then execs
the existing user shell. On successful reap the kernel emits `QS:WAIT_OK`.

- [ ] **Step 6: Verify M2C1 on real QEMU**

Run M1/M2B builds, then a temporary M2C build requiring `QS:WAIT_OK` and
`QS:IPI_OK`. Expected: old stages still boot and the M2C wait test completes.

- [ ] **Step 7: Commit M2C1**

Commit as `feat: add m2c wait primitives and scheduler ipi` and push the current
branch before starting shootdown work.

### Task 4: Add Conservative TLB Shootdown

**Files:**
- Modify: `kernel/include/timeros/sbi.h`
- Modify: `kernel/src/sbi.c`
- Modify: `kernel/include/timeros/address.h`
- Modify: `kernel/src/address.c`
- Modify: `kernel/src/task.c`

- [ ] **Step 1: Add the SBI RFENCE wrapper**

```c
struct sbiret sbi_remote_sfence_vma(u64 hart_mask, u64 hart_mask_base,
                                    u64 start, u64 size);
```

Use extension `SBI_EXT_RFENCE`, function 1.

- [ ] **Step 2: Implement full broadcast shootdown**

Build the mask from every other online normal hart. Execute local
`sfence_vma()`, then remote `sfence.vma` with start 0 and size `~0ULL`. Panic on
any SBI error and emit `QS:RFENCE_OK` after the first completed remote fence.

- [ ] **Step 3: Fence before page-table reclamation**

Call shootdown before `proc_freepagetable()` unmaps or frees any user mapping.
Do not return leaf or page-table pages to the allocator until the remote SBI
call completes.

- [ ] **Step 4: Verify real RFENCE execution**

The M2C init process `exec()` path must trigger a remote fence and produce
`QS:RFENCE_OK`. Run the M2C smoke and confirm OpenSBI reports no RFENCE error.

### Task 5: Instantiate and Map QEMU Test Exit

**Files:**
- Modify: `patches/qemu/0001-add-quard-star-machine.patch`
- Modify: `platform/quard-star/dts/quard_star_kernel.dts`
- Modify: `kernel/include/timeros/memory.h`
- Modify: `kernel/src/address.c`
- Create: `kernel/include/timeros/selftest.h`
- Create: `kernel/src/selftest.c`
- Modify: `kernel/Makefile`

- [ ] **Step 1: Add the machine device**

Add `QUARD_STAR_TEST = { 0x00100000, 0x1000 }` to the QEMU memory map and call:

```c
sifive_test_create(quard_star_memmap[QUARD_STAR_TEST].base);
```

- [ ] **Step 2: Describe and map the device**

Add a `sifive,test1` DT node and map the MMIO page read/write in `kvmmake()`.
Define the address and `0x5555` pass / `0x3333` fail values in first-party
kernel headers.

- [ ] **Step 3: Coordinate completion markers**

`selftest.c` records allocator, wait, IPI, RFENCE, migration, and minimum-time
completion. Only when every required bit is set may it print the stage marker
and write the QEMU pass value.

- [ ] **Step 4: Verify guest-driven exit**

Run QEMU directly and confirm it exits with status 0 after the serial pass
marker. A forced host-side kill is not a passing result for M2C.

### Task 6: Add Quick and Long M2C Profiles

**Files:**
- Modify: `kernel/Makefile`
- Modify: `user/Makefile`
- Modify: `scripts/m1-build.sh`
- Create: `scripts/m2c-build.sh`
- Create: `scripts/m2c-smoke.sh`
- Create: `scripts/m2c-stress.sh`
- Modify: `kernel/src/main.c`
- Modify: `kernel/src/task.c`
- Modify: `Makefile`

- [ ] **Step 1: Pass stage-specific CPP flags**

Add `CPPFLAGS` to kernel and user compilation. `m1-build.sh` passes
`QS_KERNEL_CPPFLAGS` to both builds while empty defaults preserve old stages.

- [ ] **Step 2: Parameterize stress thresholds**

Default M2B values remain 10000 allocator loops per hart and 100 migrations.
M2C quick mode uses a short target; stress mode uses at least 50000 allocator
loops per hart, 10000 migrations, and 120 seconds minimum elapsed time.

- [ ] **Step 3: Add build/smoke/stress scripts**

`m2c-build` writes `out/m2c`; `m2c-stress` writes `out/m2c-stress`. Both reuse
the M2 DTBs and existing build script. Smoke requires exit 0 plus quick markers;
stress allows a 150-second timeout and requires stress markers and counts.

- [ ] **Step 4: Verify script RED/GREEN behavior**

Fake-QEMU tests cover complete markers, missing markers, and nonzero guest exit.
Run `make test-host` and require every test to pass.

### Task 7: Verify and Publish M2C

- [ ] **Step 1: Run all host checks**

Run: `make check-env check-sources test-host`

- [ ] **Step 2: Run earlier-stage regressions**

Run M1 build/smoke, M2A build/smoke, and M2B build/smoke.

- [ ] **Step 3: Run M2C quick smoke**

Require wait, IPI, RFENCE, allocator, scheduler, pass marker, and QEMU exit 0.

- [ ] **Step 4: Run the 120-second M2C stress target**

Require at least 100000 page operations, 10000 migrations, 120 seconds elapsed,
no `QS:TEST_FAIL`, and QEMU exit 0.

- [ ] **Step 5: Inspect and publish**

Run `git diff --check`, inspect the complete staged diff and executable modes,
commit as `feat: complete dual-hart m2 synchronization`, push
`codex/m2-smp`, and confirm local/remote HEAD equality with a clean worktree.
