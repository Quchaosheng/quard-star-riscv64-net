# M2B Scheduler and Timer Design

## Goal

Run the existing process model on hart 0 and hart 1 with one shared runnable
set, safe timer preemption, and observable cross-hart process migration. Keep
the existing kernel stacks, trap frames, syscall ABI, M1, and M2A behavior.

## Scheduler

- Extend each `struct cpu` with its current process, scheduler context, idle
  state, timer deadline/ticks, and `need_resched` flag.
- Replace the global `_current` index with `cpu_this()->proc`.
- Use one global task-table lock for runnable-state transitions, PID allocation,
  and parent/child scans. This is deliberately simpler than per-process locks
  for the current ten-entry process table.
- Run one scheduler loop per online hart. A scheduler selects a `Ready` process,
  marks it `Running`, records the current hart, and switches through that hart's
  scheduler context.
- Hold the task-table lock across the scheduler/process context-switch boundary.
  The resumed side releases it only after the previous context is fully saved,
  so another hart cannot enter the same process context concurrently.
- Prefer a runnable process last executed by the other hart when an online hart
  is idle. This gives deterministic migration without introducing per-CPU run
  queues or load balancing.

## Trap and Timer

- Add a kernel `tp` value to each user trap frame. `trap_return()` writes the
  current CPU pointer and the trampoline restores it before entering C, so a
  migrated process always sees the correct per-CPU record.
- Initialize a timer independently on every online hart.
- A timer interrupt programs that hart's next deadline and sets
  `need_resched`. It does not switch while handling a kernel-mode interrupt.
- User traps and syscall/yield paths are scheduling safe points. They consume
  `need_resched` and yield through the normal process-to-scheduler switch.
- An idle scheduler enables interrupts and executes `wfi`. Local timer
  interrupts provide wakeups until remote scheduling IPI is added in M2C.

## Process Lifecycle

- Preserve the current `fork`, `exec`, `wait`, and `exit` interfaces.
- Reserve an unused task entry while it is being constructed so no scheduler
  can run a partial process.
- Protect task count, state, PID, and parent/child changes with the task-table
  lock. A running process retains exclusive access to its private address-space
  fields.
- A first-run trampoline releases the scheduler lock before entering the
  existing `trap_return()` path.

## Verification

- Add host contracts for per-CPU scheduler/timer fields, the shared task lock,
  safe-point rescheduling, kernel `tp` restoration, and M2B scripts.
- Add `m2b-build` and `m2b-smoke` targets using the existing dual-hart DTBs.
- Emit `QS:SMP_SCHED_OK` after at least 100 observed cross-hart migrations and
  `QS:TEST_PASS:m2b-smoke` afterward.
- Keep `QS:TEST_PASS:m1-smoke` and `QS:TEST_PASS:m2a-smoke` available to their
  existing smoke scripts.
- M2B completion requires host tests, M1 smoke, M2A smoke, M2B build/smoke, and
  a clean diff check. The 10000-migration long stress target remains the final
  M2 acceptance test after M2C adds IPI and deterministic test exit.

## Deferred to M2C

- Remote scheduler IPI and SBI RFENCE/TLB shootdown.
- Wait queues, semaphores, sleeping locks, and interrupt-driven VirtIO waits.
- Per-CPU run queues, affinity, and load balancing.
