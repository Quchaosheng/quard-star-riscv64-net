# M2C Synchronization, Shootdown, and Stress Design

## Goal

Finish the dual-hart M2 foundation with reusable sleep-based synchronization,
remote scheduler wakeups, safe page-table reclamation, deterministic QEMU test
exit, and quick/long stress targets. Keep M1, M2A, and M2B behavior intact.

## Delivery Split

M2C is implemented as two internal checkpoints on `codex/m2-smp`:

- M2C1 adds task wait channels, counting semaphores, sleeping locks, SBI IPI,
  and a real `fork/yield/exit/wait` wakeup path.
- M2C2 adds SBI RFENCE, QEMU test-exit, stage-specific stress settings, and
  final M2 verification.

## Wait Primitives

- Add a `Sleeping` task state, wait channel, absolute timeout, and wake result
  to each task.
- Implement wait queues by sleeping tasks on a channel while the existing task
  table lock serializes state changes. The caller's spinlock is released only
  after the task lock is held, preventing lost wakeups.
- The scheduler checks expired deadlines before selecting a runnable task, so
  timed waits also work when every process is asleep.
- Build counting semaphores and sleeping locks on the wait-queue API. A task
  must not sleep while holding an unrelated spinlock.
- Give each process a child-exit semaphore. `wait()` scans for zombies and then
  sleeps on that semaphore instead of repeatedly yielding. Child exit posts the
  parent's semaphore.

## Scheduler IPI

- Add the SBI v0.2 IPI wrapper using a hart mask and mask base.
- Enable supervisor software interrupts on every scheduling hart.
- A wakeup sends an IPI to online idle harts. The software-interrupt handler
  clears `SSIP`, sets `need_resched`, and returns through the existing safe
  scheduling point.
- Timer wakeups remain as a fallback, but correctness must not depend on a
  fixed delay.

## TLB Shootdown

- Add the SBI RFENCE remote `sfence.vma` wrapper and check every SBI error.
- All current user address spaces use ASID 0. Before unmapping and freeing a
  user page table, execute a local full `sfence.vma` and request a remote full
  fence on every other online normal hart.
- The broadcast is intentionally conservative for at most seven harts. Per-ASID
  range tracking is deferred until the kernel assigns nonzero ASIDs.
- Physical pages are returned to the allocator only after RFENCE completes.

## Self-Test and Exit

- The test init process forks a child, lets it yield, waits for its exit through
  the child semaphore, and then continues into the existing user shell.
- Stable markers are `QS:WAIT_OK`, `QS:IPI_OK`, `QS:RFENCE_OK`, and the existing
  allocator/scheduler markers.
- Instantiate QEMU's existing SiFive test device at `0x00100000`, describe it
  in the kernel DTB, and map it in the kernel page table.
- M2C test builds write `0x5555` to the device only after all required markers
  have completed. The smoke script requires both serial markers and QEMU exit
  status 0.

## Stress Profiles

- `m2c-smoke` uses the dual-hart platform, a short migration target, and a quick
  allocator test suitable for routine development.
- `m2c-stress` keeps both harts active for at least 120 seconds and requires at
  least 100000 total page allocate/free operations and 10000 cross-hart
  migrations before signaling success.
- Build-time test-profile macros affect only M2C output directories. Normal M1,
  M2A, and M2B builds retain their existing thresholds and do not exit QEMU.
- Final free-page count must equal its baseline; any SBI, timeout, wait, or
  memory error emits `QS:TEST_FAIL` and must not signal test success.

## Deferred

- Per-CPU run queues, ASID allocation, and range-coalesced shootdowns.
- Interrupt-driven VirtIO block waiting, which remains part of M3.
- Lock dependency graphs and priority inheritance.
