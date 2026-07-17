# M1 Single-Core Baseline Implementation Plan

> **For agentic workers:** Execute this plan task-by-task with verification after each step.

**Goal:** Build and smoke-test a single-hart quard-star firmware baseline from the locked first-party sources and clean third-party submodules.

**Architecture:** Migrate source files into first-party directories, build the existing kernel and trusted-domain images with small wrapper Makefiles, and run a bounded one-hart QEMU serial smoke test for the kernel. Generated artifacts stay in `out/m1/`; submodules remain untouched.

**Tech Stack:** Bash, GNU Make, RISC-V GCC, OpenSBI v1.2, QEMU v8.0.2, dtc, existing C/assembly sources.

---

### Task 1: Inventory source build contracts

**Files:**
- Read: `out/sources/kernel-src/build.sh`
- Read: `out/sources/kernel-src/os/Makefile`
- Read: `out/sources/kernel-src/trusted_domain/Makefile`
- Read: `out/sources/kernel-src/dts/quard_star_sbi.dts`

- [ ] **Step 1: Record source inventory**

Run:

```bash
find out/sources/kernel-src/boot out/sources/kernel-src/dts out/sources/kernel-src/os out/sources/kernel-src/trusted_domain -type f -print
```

- [ ] **Step 2: Verify the old build inputs**

Run:

```bash
grep -nE 'CROSS_PREFIX|PLATFORM=|fw_jump|quard_star|trusted_domain|os/|user/' out/sources/kernel-src/build.sh out/sources/kernel-src/os/Makefile out/sources/kernel-src/trusted_domain/Makefile
```

Expected: the output identifies the existing linker scripts, startup objects, OpenSBI platform, and firmware layout without referencing TCP/IP sources.

### Task 2: Extract platform patches

**Files:**
- Create: `patches/qemu/0001-add-quard-star-machine.patch`
- Create: `patches/qemu/series`
- Create: `patches/opensbi/0001-add-quard-star-platform.patch`
- Create: `patches/opensbi/series`

- [ ] **Step 1: Generate QEMU patch from the fixed legacy tree**

Include only `hw/riscv/quard_star.c`, `include/hw/riscv/quard_star.h`, `hw/riscv/Kconfig`, `hw/riscv/meson.build`, and the RISC-V device default configuration changes.

- [ ] **Step 2: Generate OpenSBI platform patch**

Include only `platform/quard_star/Kconfig`, `objects.mk`, `platform.c`, and `configs/defconfig`.

- [ ] **Step 3: Verify both patches against locked upstreams**

Run:

```bash
git -C third_party/qemu apply --check ../../patches/qemu/0001-add-quard-star-machine.patch
git -C third_party/opensbi apply --check ../../patches/opensbi/0001-add-quard-star-platform.patch
```

### Task 3: Migrate first-party source files

**Files:**
- Create: `platform/quard-star/`
- Create: `kernel/`
- Create: `trusted/`
- Create: `user/`

- [ ] **Step 1: Copy only approved source directories**

Run:

```bash
mkdir -p platform/quard-star kernel trusted user
cp -a out/sources/kernel-src/boot platform/quard-star/boot
cp -a out/sources/kernel-src/dts platform/quard-star/dts
cp -a out/sources/kernel-src/os/. kernel/
cp -a out/sources/kernel-src/trusted_domain/. trusted/
```

- [ ] **Step 2: Remove copied vendored trees**

Run:

```bash
rm -rf trusted/FreeRTOS-Kernel
```

The trusted build must reference `third_party/freertos` instead.

- [ ] **Step 3: Add migration manifest**

Create `platform/quard-star/M1-SOURCES.txt` containing the two source commit IDs and the exact copied directories. The manifest is checked into Git and must not mention QEMU/OpenSBI source copies.

### Task 4: Add minimal build wrappers

**Files:**
- Create: `platform/quard-star/Makefile`
- Create: `kernel/Makefile`
- Create: `trusted/Makefile`
- Modify: `Makefile`

- [ ] **Step 1: Write kernel wrapper**

The wrapper invokes the migrated kernel Makefile with `CROSS_COMPILE=riscv64-unknown-elf-` and writes all output below `out/m1/kernel`.

- [ ] **Step 2: Write trusted wrapper**

The wrapper invokes the migrated trusted-domain build with `FREERTOS_DIR=$(abspath third_party/freertos)` and writes output below `out/m1/trusted`.

- [ ] **Step 3: Add top-level targets**

Add `m1-build` and `m1-smoke` targets while preserving `check-env`, `deps`, `check-sources`, and `test-host`.

### Task 5: Add bounded smoke test

**Files:**
- Create: `scripts/m1-smoke.sh`
- Create: `tests/host/test_m1_smoke_script.sh`

- [ ] **Step 1: Write the failing host test**

The host test uses a temporary fake QEMU that emits the three kernel success markers and verifies exit code zero; it also verifies `-smp 1`. A second fake QEMU omits one marker and must fail.

- [ ] **Step 2: Implement the smoke runner**

The runner captures serial output to `out/m1/qemu.log`, kills QEMU after 20 seconds, rejects `QS:TEST_FAIL`, and requires every success marker.

- [ ] **Step 3: Run the host regression test**

Run:

```bash
tests/host/test_m1_smoke_script.sh
```

Expected: `PASS: M1 smoke script behavior`.

### Task 6: Verify and commit M1 baseline

- [ ] **Step 1: Run focused checks**

```bash
bash -n scripts/m1-smoke.sh tests/host/test_m1_smoke_script.sh tests/host/test_m1_dts.sh
tests/host/test_m1_dts.sh
make check-sources
make m1-build
tests/host/test_m1_build_contracts.sh
make m1-smoke
git diff --check
```

- [ ] **Step 2: Inspect generated markers**

```bash
grep -E 'QS:(BOOT_OK|KERNEL_READY|TEST_PASS:m1-smoke)' out/m1/qemu.log
```

- [ ] **Step 3: Commit**

```bash
git add Makefile platform kernel trusted user scripts tests docs
git commit -m "feat: add single-core m1 baseline"
git push
```
