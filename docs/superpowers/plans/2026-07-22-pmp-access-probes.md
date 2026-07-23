# PMP Access Probes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove with recoverable QEMU faults that PMP denies load, store, and instruction access in both directions between the ordinary and trusted domains.

**Architecture:** Extend each domain's existing serial probe state with an expected cause, fault address, and recovery PC. The ordinary kernel returns the recovery PC from its self-test fault handler; the FreeRTOS trap path passes the saved task PC slot to its C exception handler so an instruction fault can resume outside the denied target.

**Tech Stack:** C11, RISC-V64 inline assembly, FreeRTOS RISC-V S-mode port assembly, POSIX shell contract tests, QEMU quard-star M8 smoke test.

---

## File Map

- `tests/host/test_m8_contracts.sh`: static contracts for causes, recovery interfaces, marker wiring, and test-only page permissions.
- `kernel/include/timeros/riscv.h`: shared synchronous exception cause constants.
- `kernel/include/timeros/selftest.h`: ordinary-domain PMP fault-handler declaration.
- `kernel/src/address.c`: test-only `R|W|X` mapping of trusted RAM.
- `kernel/src/selftest.c`: ordinary-domain probe state, three access attempts, and markers.
- `kernel/src/trap.c`: recover matched ordinary-domain PMP faults at the recorded PC.
- `trusted/main.c`: trusted-domain probe state, exception matching, three access attempts, and markers.
- `trusted/port/portASM.S`: pass the saved task PC slot to the trusted C exception handler.
- `scripts/m8-smoke.sh`: require all six specific markers and both aggregate markers.
- `tests/host/test_m9_contracts.sh`: contract for the updated QEMU-only limitation statement.
- `docs/limitations.md`: state exactly which access classes QEMU validates and which boundaries remain.

### Task 1: Ordinary-Domain Load, Store, And Execute Probes

**Files:**
- Modify: `tests/host/test_m8_contracts.sh`
- Modify: `kernel/include/timeros/riscv.h`
- Modify: `kernel/include/timeros/selftest.h`
- Modify: `kernel/src/address.c`
- Modify: `kernel/src/selftest.c`
- Modify: `kernel/src/trap.c`
- Modify: `scripts/m8-smoke.sh`

- [ ] **Step 1: Add failing ordinary-domain contracts**

Append checks that require the three ordinary markers, the generalized handler,
all three synchronous causes, and an `R|W|X` test mapping:

```sh
for access in LOAD STORE EXEC; do
  grep -Fq "QS:PMP_UNTRUSTED_${access}_DENY_OK" \
    "$root/kernel/src/selftest.c"
  grep -Fq "QS:PMP_UNTRUSTED_${access}_DENY_OK" \
    "$root/scripts/m8-smoke.sh"
done
grep -Fq 'm9_pmp_handle_fault' "$root/kernel/include/timeros/selftest.h"
grep -Fq 'm9_pmp_handle_fault' "$root/kernel/src/trap.c"
grep -Fq '#define EXC_INST_ACCESS  1' "$root/kernel/include/timeros/riscv.h"
grep -Fq '#define EXC_LOAD_ACCESS  5' "$root/kernel/include/timeros/riscv.h"
grep -Fq '#define EXC_STORE_ACCESS 7' "$root/kernel/include/timeros/riscv.h"
grep -Fq 'PTE_R | PTE_W | PTE_X' "$root/kernel/src/address.c"
```

- [ ] **Step 2: Run the contract and verify RED**

Run:

```powershell
& 'C:\Program Files\Git\bin\bash.exe' -lc './tests/host/test_m8_contracts.sh'
```

Expected: FAIL because `QS:PMP_UNTRUSTED_LOAD_DENY_OK` is absent.

- [ ] **Step 3: Add shared exception constants and generalize the handler API**

Add to `kernel/include/timeros/riscv.h`:

```c
#define EXC_INST_ACCESS  1
#define EXC_LOAD_ACCESS  5
#define EXC_STORE_ACCESS 7
```

Replace the load-only declaration in `kernel/include/timeros/selftest.h` with:

```c
int m9_pmp_handle_fault(reg_t cause, reg_t stval, reg_t *resume_pc);
```

- [ ] **Step 4: Allow all three page-table permissions in the test mapping**

Change the `QS_M9_PMP_TEST` mapping in `kernel/src/address.c` to:

```c
PageTable_map(&pt, virt_addr_from_size_t(PMP_PROBE_VA),
              phys_addr_from_size_t(PMP_TRUSTED_BASE), PAGE_SIZE,
              PTE_R | PTE_W | PTE_X);
```

- [ ] **Step 5: Implement the serial ordinary-domain probe state**

Replace the load-only state and handler in `kernel/src/selftest.c` with one
expected-fault record. Use label addresses as the recovery PCs and keep each
attempt inline so the compiler cannot hide control flow behind a new ABI:

```c
static volatile u32 pmp_probe_state;
static reg_t pmp_probe_cause;
static reg_t pmp_probe_resume;

static void m9_pmp_arm(reg_t cause, void *resume)
{
    pmp_probe_cause = cause;
    pmp_probe_resume = (reg_t)(uintptr_t)resume;
    __atomic_store_n(&pmp_probe_state, 1, __ATOMIC_RELEASE);
}

static void m9_pmp_require(const char *marker)
{
    if (__atomic_load_n(&pmp_probe_state, __ATOMIC_ACQUIRE) != 2) {
        printk("QS:PMP_UNTRUSTED_DENY_FAIL\n");
        panic("trusted memory access was not denied");
    }
    printk("%s\n", marker);
}

int m9_pmp_handle_fault(reg_t cause, reg_t stval, reg_t *resume_pc)
{
    if (__atomic_load_n(&pmp_probe_state, __ATOMIC_ACQUIRE) != 1 ||
        cause != pmp_probe_cause || stval != PMP_PROBE_VA)
        return 0;
    *resume_pc = pmp_probe_resume;
    __atomic_store_n(&pmp_probe_state, 2, __ATOMIC_RELEASE);
    return 1;
}
```

In `m9_pmp_probe()`, arm and run three explicit operations. Each `resume_*`
label is both a normal fall-through failure point and the trap recovery point:

```c
m9_pmp_arm(EXC_LOAD_ACCESS, &&resume_load);
asm volatile("lw zero, 0(%0)" :: "r"((uintptr_t)PMP_PROBE_VA) : "memory");
resume_load:
m9_pmp_require("QS:PMP_UNTRUSTED_LOAD_DENY_OK");

m9_pmp_arm(EXC_STORE_ACCESS, &&resume_store);
asm volatile("sw zero, 0(%0)" :: "r"((uintptr_t)PMP_PROBE_VA) : "memory");
resume_store:
m9_pmp_require("QS:PMP_UNTRUSTED_STORE_DENY_OK");

m9_pmp_arm(EXC_INST_ACCESS, &&resume_exec);
asm volatile("jr %0" :: "r"((uintptr_t)PMP_PROBE_VA) : "memory");
resume_exec:
m9_pmp_require("QS:PMP_UNTRUSTED_EXEC_DENY_OK");
printk("QS:PMP_UNTRUSTED_DENY_OK\n");
```

- [ ] **Step 6: Recover matched kernel faults at the recorded PC**

Remove the local load-only exception constant from `kernel/src/trap.c`. Replace
the load-only branch in `kerneltrap()` with:

```c
#ifdef QS_M9_PMP_TEST
reg_t cause_code = scause & SCAUSE_CODE_MASK;
reg_t resume_pc;
if ((scause & SCAUSE_INTERRUPT_MASK) == 0 &&
    m9_pmp_handle_fault(cause_code, r_stval(), &resume_pc)) {
    w_sepc(resume_pc);
    w_sstatus(sstatus);
    return;
}
#endif
```

The probe handler rejects causes other than the armed cause, so ordinary kernel
exceptions retain the existing panic behavior.

- [ ] **Step 7: Require the ordinary markers in M8 smoke**

Add after the existing aggregate ordinary marker check in `scripts/m8-smoke.sh`:

```sh
for access in LOAD STORE EXEC; do
  grep -q "QS:PMP_UNTRUSTED_${access}_DENY_OK" "$root/out/m8/qemu.log"
done
```

- [ ] **Step 8: Run the ordinary contract and verify GREEN**

Run:

```powershell
& 'C:\Program Files\Git\bin\bash.exe' -lc './tests/host/test_m8_contracts.sh'
```

Expected: `PASS: M8 trusted-domain and seven-hart contracts`.

- [ ] **Step 9: Commit the ordinary-domain probe**

```powershell
git add tests/host/test_m8_contracts.sh kernel/include/timeros/riscv.h `
  kernel/include/timeros/selftest.h kernel/src/address.c kernel/src/selftest.c `
  kernel/src/trap.c scripts/m8-smoke.sh
git commit -m "feat: probe ordinary pmp access faults"
```

### Task 2: Trusted-Domain Load, Store, And Execute Probes

**Files:**
- Modify: `tests/host/test_m8_contracts.sh`
- Modify: `trusted/main.c`
- Modify: `trusted/port/portASM.S`
- Modify: `scripts/m8-smoke.sh`

- [ ] **Step 1: Add failing trusted-domain contracts**

Add these checks to `tests/host/test_m8_contracts.sh`:

```sh
for access in LOAD STORE EXEC; do
  grep -Fq "QS:PMP_TRUSTED_${access}_DENY_OK" "$root/trusted/main.c"
  grep -Fq "QS:PMP_TRUSTED_${access}_DENY_OK" "$root/scripts/m8-smoke.sh"
done
grep -Fq 'uintptr_t *saved_pc' "$root/trusted/main.c"
grep -Fq 'addi a1, sp, 0' "$root/trusted/port/portASM.S"
```

- [ ] **Step 2: Run the contract and verify RED**

Run:

```powershell
& 'C:\Program Files\Git\bin\bash.exe' -lc './tests/host/test_m8_contracts.sh'
```

Expected: FAIL because `QS:PMP_TRUSTED_LOAD_DENY_OK` is absent.

- [ ] **Step 3: Pass the saved task PC slot to the C handler**

In `trusted/port/portASM.S`, update only the synchronous-exception setup:

```asm
synchronous_exception:
    addi a1, a1, 4
    store_x a1, 0(sp)
    addi a1, sp, 0
    load_x sp, xISRStackTop
    j handle_exception
```

`a1` now points at the saved PC slot. Breakpoint yield does not call the C
exception handler, so its behavior remains unchanged.

- [ ] **Step 4: Generalize the trusted exception handler**

In `trusted/main.c`, keep one serial expected-fault record:

```c
static volatile uintptr_t pmp_probe_state;
static uintptr_t pmp_probe_cause;
static uintptr_t pmp_probe_address;
static uintptr_t pmp_probe_resume;

void freertos_risc_v_application_exception_handler(uintptr_t cause,
                                                     uintptr_t *saved_pc)
{
    if (pmp_probe_state == 1 && cause == pmp_probe_cause &&
        csr_read(CSR_STVAL) == pmp_probe_address) {
        *saved_pc = pmp_probe_resume;
        pmp_probe_state = 2;
        return;
    }

    switch (cause) {
    case 1:
        _puts("QS:TRUSTED_EXCEPTION:INSTRUCTION_ACCESS\n");
        break;
    case 5:
        _puts("QS:TRUSTED_EXCEPTION:LOAD_ACCESS\n");
        break;
    case 7:
        _puts("QS:TRUSTED_EXCEPTION:STORE_ACCESS\n");
        break;
    default:
        _puts("QS:TRUSTED_EXCEPTION:OTHER\n");
        break;
    }
    for (;;)
        __asm volatile("wfi");
}
```

- [ ] **Step 5: Run the three trusted access attempts**

Add these helpers before `acceptance_task()`:

```c
static void pmp_probe_arm(uintptr_t cause, uintptr_t address, void *resume)
{
    pmp_probe_cause = cause;
    pmp_probe_address = address;
    pmp_probe_resume = (uintptr_t)resume;
    pmp_probe_state = 1;
    __asm volatile("" ::: "memory");
}

static void pmp_probe_require(const char *marker)
{
    __asm volatile("" ::: "memory");
    if (pmp_probe_state != 2) {
        _puts("QS:PMP_TRUSTED_DENY_FAIL\n");
        for (;;)
            __asm volatile("wfi");
    }
    _puts(marker);
    _puts("\n");
}
```

Then run the three probes in `acceptance_task()`:

```c
pmp_probe_arm(5, 0x80200000UL, &&resume_load);
__asm volatile("lw zero, 0(%0)" :: "r"((uintptr_t)0x80200000UL) : "memory");
resume_load:
pmp_probe_require("QS:PMP_TRUSTED_LOAD_DENY_OK");

pmp_probe_arm(7, 0x80200000UL, &&resume_store);
__asm volatile("sw zero, 0(%0)" :: "r"((uintptr_t)0x80200000UL) : "memory");
resume_store:
pmp_probe_require("QS:PMP_TRUSTED_STORE_DENY_OK");

pmp_probe_arm(1, 0x80200000UL, &&resume_exec);
__asm volatile("jr %0" :: "r"((uintptr_t)0x80200000UL) : "memory");
resume_exec:
pmp_probe_require("QS:PMP_TRUSTED_EXEC_DENY_OK");
_puts("QS:PMP_TRUSTED_DENY_OK\n");
```

The helpers use compiler memory barriers because the trap changes the state
outside the normal C control flow.

- [ ] **Step 6: Require the trusted markers in M8 smoke**

Add after the existing aggregate trusted marker check:

```sh
for access in LOAD STORE EXEC; do
  grep -q "QS:PMP_TRUSTED_${access}_DENY_OK" "$QS_TRUSTED_SERIAL_LOG"
done
```

- [ ] **Step 7: Run the contract and verify GREEN**

Run:

```powershell
& 'C:\Program Files\Git\bin\bash.exe' -lc './tests/host/test_m8_contracts.sh'
```

Expected: `PASS: M8 trusted-domain and seven-hart contracts`.

- [ ] **Step 8: Commit the trusted-domain probe**

```powershell
git add tests/host/test_m8_contracts.sh trusted/main.c trusted/port/portASM.S `
  scripts/m8-smoke.sh
git commit -m "feat: probe trusted pmp access faults"
```

### Task 3: Document The Expanded QEMU Evidence

**Files:**
- Modify: `tests/host/test_m9_contracts.sh`
- Modify: `docs/limitations.md`

- [ ] **Step 1: Add a failing limitation contract**

Add to `tests/host/test_m9_contracts.sh`:

```sh
grep -Fq 'load, store, and instruction access faults' \
  "$root/docs/limitations.md"
```

- [ ] **Step 2: Run the contract and verify RED**

Run:

```powershell
& 'C:\Program Files\Git\bin\bash.exe' -lc './tests/host/test_m9_contracts.sh'
```

Expected: FAIL because the existing limitation mentions only bidirectional
fault probes without enumerating access classes.

- [ ] **Step 3: Update the limitation without expanding claims**

Replace the PMP limitation bullet with:

```markdown
- OpenSBI PMP isolates hart7's 8 MiB trusted RAM and UART2 from harts 0-6, and isolates ordinary RAM from hart7. The load, store, and instruction access faults are tested bidirectionally and remain QEMU-only evidence; there is no physical-board, DMA-isolation, or side-channel claim.
```

- [ ] **Step 4: Run M8 and M9 contracts and verify GREEN**

Run:

```powershell
& 'C:\Program Files\Git\bin\bash.exe' -lc './tests/host/test_m8_contracts.sh && ./tests/host/test_m9_contracts.sh'
```

Expected: both scripts print `PASS`.

- [ ] **Step 5: Commit the evidence boundary**

```powershell
git add tests/host/test_m9_contracts.sh docs/limitations.md
git commit -m "docs: record pmp access probe boundary"
```

### Task 4: Build, QEMU Acceptance, And Branch Verification

**Files:**
- Verify only; modify implementation files only if a failing test identifies a defect.

- [ ] **Step 1: Run focused local contracts**

```powershell
& 'C:\Program Files\Git\bin\bash.exe' -lc './tests/host/test_m8_contracts.sh && ./tests/host/test_m9_contracts.sh'
```

Expected: both scripts print `PASS`.

- [ ] **Step 2: Run the complete host suite in an environment with `dtc`**

```sh
make test-host
```

Expected: every host script passes. On the current Windows host, GitHub Actions
is the authoritative complete-host result because `dtc` is unavailable.

- [ ] **Step 3: Build and run full M8 acceptance**

```sh
make m8-build
sudo -v
make m8-smoke
```

Expected: the QEMU log contains all three ordinary markers and the ordinary
aggregate marker; UART2 contains all three trusted markers, the trusted
aggregate marker, and `QS:TRUSTED_SCHED_OK`.

- [ ] **Step 4: Inspect the actual QEMU logs**

```sh
grep -E 'QS:PMP_(UNTRUSTED|TRUSTED)_(LOAD|STORE|EXEC)_DENY_OK' \
  out/m8/qemu.log out/m8/trusted.log
grep -E 'QS:PMP_(UNTRUSTED|TRUSTED)_DENY_OK|QS:TRUSTED_SCHED_OK' \
  out/m8/qemu.log out/m8/trusted.log
```

Expected: six specific markers, two aggregate markers, and the scheduler marker.

- [ ] **Step 5: Run final repository checks**

```powershell
git diff --check origin/main...HEAD
git status --short --branch
git log --oneline origin/main..HEAD
```

Expected: no whitespace errors, a clean worktree, and only the design plus
ordinary, trusted, and boundary commits.
