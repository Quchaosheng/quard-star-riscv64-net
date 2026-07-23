# PMP Access Probe Design

## Goal

Extend the existing bidirectional QEMU PMP acceptance check from load access
faults to load, store, and instruction access faults. Keep the current PMP
layout and FreeRTOS scheduler unchanged.

## Scope

- Probe ordinary-domain access to trusted RAM.
- Probe trusted-domain access to ordinary RAM.
- Require recoverable load, store, and instruction access faults in both
  directions.
- Preserve the existing aggregate success markers for compatibility.
- Add per-access markers so the M8 log proves which checks ran.

Physical-board testing, DMA isolation, side-channel analysis, PMP layout
changes, and a new release are outside this change.

## Probe State

Each domain keeps one small probe state with:

- the expected synchronous exception cause;
- the expected fault address;
- the instruction address at which execution must resume;
- whether the expected fault was observed.

The trap path handles a fault only when all expected values match. Any other
exception follows the existing fatal path. Probes run serially, so no queue or
multi-probe abstraction is needed.

## Ordinary Domain

The test-only virtual mapping for trusted RAM uses `PTE_R | PTE_W | PTE_X`.
This ensures that page-table permissions allow each attempted access and that
the observed denial comes from PMP.

The kernel probe performs load, store, and execute attempts in order. Load and
store resume after the faulting instruction. Execute records a local recovery
label before jumping to the denied address because an instruction access
fault reports the target address in `sepc`.

`kerneltrap()` accepts exception causes 1, 5, and 7 only through the armed PMP
probe handler. The handler returns the recorded recovery address; unrelated
kernel exceptions still panic.

## Trusted Domain

The trusted task performs the same three accesses against ordinary RAM. The
FreeRTOS trap assembly passes the saved task program-counter slot to the C
exception handler. The handler updates that slot only after matching the armed
probe's cause and `stval`, allowing instruction faults to return to the local
recovery label instead of the denied target.

The existing breakpoint-based task yield and timer-interrupt paths are not
changed.

## Evidence And Failure Behavior

M8 requires these new markers:

- `QS:PMP_UNTRUSTED_LOAD_DENY_OK`
- `QS:PMP_UNTRUSTED_STORE_DENY_OK`
- `QS:PMP_UNTRUSTED_EXEC_DENY_OK`
- `QS:PMP_TRUSTED_LOAD_DENY_OK`
- `QS:PMP_TRUSTED_STORE_DENY_OK`
- `QS:PMP_TRUSTED_EXEC_DENY_OK`

After all three checks pass, each domain still emits its existing aggregate
marker:

- `QS:PMP_UNTRUSTED_DENY_OK`
- `QS:PMP_TRUSTED_DENY_OK`

An access that unexpectedly succeeds emits a failure marker when control can
return. An unexpected trap remains fatal. An unexpectedly permitted execute
may leave the probe path; the M8 marker timeout then fails acceptance.

## Tests And Documentation

1. Extend `test_m8_contracts.sh` first and verify that it fails because the new
   markers, causes, mapping permissions, and recovery interface are absent.
2. Implement the minimum ordinary and trusted probe changes.
3. Run the focused M8/M9 contracts and the complete host suite where the host
   dependencies are available.
4. Run the full M8 QEMU smoke test and require all six specific markers plus
   both aggregate markers.
5. Update the limitation text only after QEMU evidence exists. It must still
   state that physical-board, DMA, and side-channel validation are not claimed.
