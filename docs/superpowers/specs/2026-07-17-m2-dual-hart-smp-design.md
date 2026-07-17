# M2 Dual-Hart SMP Design

## Goal

Bring hart 0 and hart 1 online in the existing C kernel, make the core memory
and scheduling paths SMP-safe, and finish with a repeatable dual-hart stress
test. M1 build and smoke targets must keep working.

## Source Policy

- Reuse the first-party boot, DTS, kernel, and SBI code already migrated from
  `Quchaosheng/quard-star-riscv64-kernel` revision
  `641f42560999ab00ad7ba01169cb2b3d723d8c48`.
- Move and adapt existing project code before writing replacements.
- Use the existing OpenSBI and libfdt submodules for HSM, RFENCE, and DTB
  parsing. Do not copy their source trees into first-party directories.
- Do not import old Git history, generated files, or bundled third-party trees.

## Delivery

### M2A: Boot and Memory

- Describe hart 0 and hart 1 in the OpenSBI and kernel DTBs.
- Pass the OpenSBI hart ID and kernel DTB address through the kernel entry.
- Add per-CPU records and independent boot stacks sized for hart 0-6.
- Enumerate enabled harts from the kernel DTB with libfdt.
- Start hart 1 through SBI HSM and publish online state with release/acquire
  atomics. Remove the fixed-delay secondary boot wait.
- Add spinlocks and replace the fixed recycled-page array with a locked free
  page list.
- Run concurrent page allocation/free checks on both harts.

### M2B: Scheduler and Timer

- Give each CPU its own current process, scheduler context, idle state, and
  timer state.
- Protect process state and the single global runnable set with locks.
- Run one scheduler loop per hart and allow runnable processes to migrate.
- Make timer interrupts request rescheduling and switch only at safe points.
- Preserve the existing process kernel stacks, trap frames, and syscall ABI.

### M2C: Synchronization and Shootdown

- Add wait queues, counting semaphores, and sleeping locks.
- Use SBI IPI for remote scheduling wakeups.
- Use local `sfence.vma` plus SBI RFENCE before reusing pages that were visible
  to another online hart.
- Add the quard-star QEMU test/exit device at `0x00100000`.
- Add quick dual-hart smoke and configurable long stress targets.

## Rules

- Hart counts come from the kernel DTB; only the maximum supported CPU count
  may be a compile-time constant.
- No fixed-delay inter-hart synchronization and no `volatile` in place of
  atomics.
- Keep one global runnable set; per-CPU run queues and load balancing are later
  work.
- VirtIO interrupt-driven waiting remains M3. M2 only keeps the M1 block smoke
  working while the scheduler becomes SMP-safe.
- Hart 7 remains reserved for the trusted domain and is not part of M2.

## Acceptance

- Serial output contains `QS:HART_ONLINE:0`, `QS:HART_ONLINE:1`,
  `QS:SMP_ALLOC_OK`, and the stage-specific `QS:TEST_PASS` marker.
- The long test completes at least 100000 page allocate/free operations and
  10000 cross-hart task migrations.
- Free-page count returns to its starting value and no lock, scheduler, HSM,
  IPI, or RFENCE error is reported.
- `make test-host`, M1 build contracts, `make m1-smoke`, M2 smoke, and the
  120-second M2 stress target all pass.
