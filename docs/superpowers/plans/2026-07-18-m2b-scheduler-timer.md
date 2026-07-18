# M2B Scheduler and Timer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run the existing process model safely on hart 0 and hart 1 and prove repeatable cross-hart migration.

**Architecture:** Keep one locked global task table and give each CPU its own current-process pointer, scheduler context, idle state, and timer state. Timer interrupts request rescheduling; process switches occur only from user-trap or syscall safe points.

**Tech Stack:** C, RISC-V assembly, GCC atomics, GNU Make, Bash, OpenSBI HSM, QEMU quard-star.

---

### Task 1: Lock M2B Contracts

**Files:**
- Create: `tests/host/test_m2b_contracts.sh`
- Create: `tests/host/test_m2b_smoke_script.sh`
- Modify: `Makefile`

- [ ] **Step 1: Add failing source contracts**

Require per-CPU `proc`, `scheduler_context`, `idle`, `timer_ticks`, and
`need_resched`; a global task lock; scheduler loop; kernel `tp` restoration;
safe-point rescheduling; and `QS:SMP_SCHED_OK`.

- [ ] **Step 2: Add a failing fake-QEMU smoke test**

The fake log must contain the M2A markers plus:

```text
QS:SMP_SCHED_OK
QS:TEST_PASS:m2b-smoke
```

The script must reject a log missing either scheduler marker.

- [ ] **Step 3: Register the scripts and verify RED**

Run: `make test-host`

Expected: existing tests pass; M2B contracts fail because the scheduler fields,
implementation, and scripts do not exist.

### Task 2: Preserve Per-CPU Identity Across User Traps

**Files:**
- Modify: `kernel/include/timeros/context.h`
- Modify: `kernel/include/timeros/cpu.h`
- Modify: `kernel/include/timeros/task.h`
- Modify: `kernel/src/kerneltrap.S`
- Modify: `kernel/src/task.c`

- [ ] **Step 1: Make context types independently includable**

Make `context.h` include `types.h`, tag `struct TaskContext`, and let `cpu.h`
embed a scheduler context without depending on the umbrella `os.h` include.
Tag `struct TaskControlBlock` so `cpu.h` can hold a pointer to it.

- [ ] **Step 2: Extend the user trap frame**

Append one field without changing existing register offsets:

```c
reg_t kernel_tp;
```

- [ ] **Step 3: Restore kernel `tp` in the trampoline**

After saving user registers and before changing to the process kernel stack:

```asm
ld tp, 37*8(sp)
ld sp, 35*8(sp)
```

Set `kernel_tp` from `cpu_this()` immediately before every `trap_return()`.

- [ ] **Step 4: Verify GREEN for the trap contract**

Run: `tests/host/test_m2b_contracts.sh`

Expected: only scheduler, timer, marker, and script requirements remain red.

### Task 3: Add Per-CPU Scheduler and Locked Runnable Set

**Files:**
- Modify: `kernel/include/timeros/cpu.h`
- Modify: `kernel/include/timeros/task.h`
- Modify: `kernel/src/task.c`
- Modify: `kernel/src/main.c`

- [ ] **Step 1: Add per-CPU scheduling state**

```c
struct TaskControlBlock *proc;
struct TaskContext scheduler_context;
u32 idle;
u32 need_resched;
u64 timer_ticks;
u64 timer_deadline;
```

- [ ] **Step 2: Replace `_current` with the current CPU's process**

`current_proc()`, `get_current_trap_cx()`, and `current_user_token()` must use
`cpu_this()->proc` and reject calls without a running process.

- [ ] **Step 3: Add one task-table lock and first-run trampoline**

Initialize the lock in `procinit()`. The scheduler holds it while selecting and
switching into a process. A new first-run entry releases it before calling the
existing user return path:

```c
static void task_first_run(void)
{
    spin_unlock(&task_lock);
    trap_return();
}
```

- [ ] **Step 4: Implement process yield and per-hart scheduler loops**

`schedule()` becomes the running process's yield operation: acquire the task
lock, mark the process `Ready`, switch to `cpu->scheduler_context`, then release
the lock after the process is selected again. `scheduler()` loops forever,
chooses one `Ready` task, sets `cpu->proc`, and idles with interrupts enabled and
`wfi` when none is runnable.

- [ ] **Step 5: Make migration deterministic without an IPI**

Record `last_hart` in each task. If another online CPU is idle, the CPU that
last ran the only runnable process defers it so the idle CPU can take it on its
next local timer wakeup. Count a migration whenever `last_hart` changes.

- [ ] **Step 6: Start schedulers on both harts**

After the M2A allocator test, hart 0 publishes a scheduler-start flag. Each
secondary initializes its local timer and enters `scheduler()`; hart 0 performs
the same initialization after existing boot work.

- [ ] **Step 7: Build and verify GREEN**

Run: `make m2a-build`

Expected: the kernel links with per-CPU scheduler contexts and no global
`_current` reference remains.

### Task 4: Defer Timer Preemption to Safe Points

**Files:**
- Modify: `kernel/src/timer.c`
- Modify: `kernel/src/trap.c`
- Modify: `kernel/src/task.c`
- Modify: `kernel/include/timeros/os.h`

- [ ] **Step 1: Record timer state per CPU**

`timer_init()` and the timer interrupt path update only the current CPU's
deadline and tick count. Publish `need_resched` with GCC atomics.

- [ ] **Step 2: Remove direct switching from interrupt dispatch**

The timer case must only re-arm the timer and request rescheduling:

```c
case IRQ_S_TIMER:
    timer_tick();
    return 1;
```

- [ ] **Step 3: Consume requests at a user-trap safe point**

After handling a user interrupt or syscall, atomically clear `need_resched` and
call `schedule()` before `trap_return()`. Kernel-mode timer traps only restore
the interrupted kernel context.

- [ ] **Step 4: Run scheduler contracts and the M2A smoke**

Run: `make test-host && make m2a-build && make m2a-smoke`

Expected: host contracts pass and all existing M2A markers remain present.

### Task 5: Protect Existing Process Lifecycle State

**Files:**
- Modify: `kernel/include/timeros/task.h`
- Modify: `kernel/src/task.c`

- [ ] **Step 1: Reserve tasks during construction**

Add a non-runnable `Creating` state. `allocproc()` reserves an `UnInit` entry
under the task lock and publishes `Ready` only after its trap frame, page table,
and context are complete.

- [ ] **Step 2: Lock PID, count, state, and relationship changes**

Protect `nextpid`, task count, parent assignment, child scans, zombie
publication, and task reuse with the task lock. Do not hold a stale process
pointer across a context switch unless it is the current running process.

- [ ] **Step 3: Preserve non-returning exit behavior**

`exit_current_and_run_next()` marks the current task `Zombie` while locked and
switches directly to the current CPU scheduler context. It must never make the
zombie runnable again.

- [ ] **Step 4: Rebuild and run existing kernel contracts**

Run: `tests/host/test_m1_kernel_contracts.sh && make m2a-build`

Expected: PASS and exit code 0.

### Task 6: Add M2B Build and Smoke Targets

**Files:**
- Create: `scripts/m2b-build.sh`
- Create: `scripts/m2b-smoke.sh`
- Modify: `Makefile`
- Modify: `kernel/src/task.c`

- [ ] **Step 1: Add the M2B build wrapper**

Reuse `scripts/m1-build.sh` with `QS_STAGE=m2b` and the existing M2 dual-hart
DTBs. Do not duplicate QEMU, OpenSBI, or kernel build logic.

- [ ] **Step 2: Emit migration completion once**

Under the task lock, count cross-hart selections. At 100 migrations emit:

```text
QS:SMP_SCHED_OK
QS:TEST_PASS:m2b-smoke
```

- [ ] **Step 3: Add the M2B smoke wrapper**

Run QEMU with `-smp 2`, reject `QS:TEST_FAIL`, and require all boot, allocator,
hart-online, scheduler, and M2B pass markers.

- [ ] **Step 4: Verify the script tests turn GREEN**

Run: `make test-host`

Expected: every host script prints PASS.

### Task 7: Verify and Publish M2B

- [ ] **Step 1: Run all source and host checks**

Run: `make check-env check-sources test-host`

- [ ] **Step 2: Run single-hart regression**

Run: `tests/host/test_m1_build_contracts.sh && make m1-build && make m1-smoke`

- [ ] **Step 3: Run M2A regression**

Run: `make m2a-build && make m2a-smoke`

- [ ] **Step 4: Run the new dual-hart scheduler smoke**

Run: `make m2b-build && make m2b-smoke`

Expected markers: both harts online, allocator PASS, at least 100 migrations,
`QS:SMP_SCHED_OK`, and `QS:TEST_PASS:m2b-smoke`.

- [ ] **Step 5: Inspect, commit, and push**

Run `git diff --check`, inspect the complete staged diff and executable modes,
commit as `feat: add dual-hart m2b scheduler`, and push `codex/m2-smp`.
