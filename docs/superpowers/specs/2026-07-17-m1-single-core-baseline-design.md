# M1 Single-Core Baseline Design

## Goal

Produce a reproducible single-hart firmware baseline for the quard-star machine using the locked QEMU/OpenSBI dependencies and first-party source revisions.

## Scope

- Migrate only first-party boot, DTS, kernel, trusted-domain, and user source files.
- Keep QEMU, OpenSBI, and FreeRTOS-Kernel submodules clean and out of the migrated source tree.
- Build a kernel image and a trusted-domain image with the RISC-V bare-metal toolchain.
- Build a combined firmware image and run a bounded one-hart QEMU smoke test.
- Emit stable markers `QS:BOOT_OK`, `QS:KERNEL_READY`, and `QS:TEST_PASS:m1-smoke`.

## Non-goals

M1 does not add SMP scheduling, trusted-domain execution, TCP/IP, VirtIO networking, FatFs, socket ABI, or production OpenSBI domain isolation. Those remain later milestones. The trusted image is built to validate its migrated build contract but is not started while QEMU exposes only hart 0.

## Layout

First-party sources live under `platform/quard-star/`, `kernel/`, `trusted/`, and `user/`. Generated files live under `out/m1/` and remain ignored. The build uses the locked `third_party/opensbi` tree and applies only patches listed in `patches/opensbi/series`; no source is copied into a submodule.

The legacy QEMU and OpenSBI trees are used only as comparison inputs. The quard-star machine and OpenSBI platform are extracted into `patches/qemu/` and `patches/opensbi/`, then checked with `git apply --check` against the locked upstream revisions.

## Acceptance

1. `make m1-build` succeeds on Ubuntu 24.04 or 26.04 with the checked environment.
2. `make m1-smoke` starts `qemu-system-riscv64` headless for at most 20 seconds.
3. The serial log contains all three required markers and no `QS:TEST_FAIL` marker.
4. The test exits non-zero on timeout, missing marker, or QEMU failure.
