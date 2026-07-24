# quard-star-riscv64-net

`quard-star-riscv64-net` is an educational RISC-V64 SMP operating system for the custom QEMU quard-star machine. It combines an independently implemented C kernel, OpenSBI domains, a dedicated FreeRTOS trusted hart, VirtIO block and network devices, and the author's tiny TCP/IP stack.

The implementation currently boots seven ordinary SMP harts and one isolated FreeRTOS trusted hart. The kernel includes VirtIO block/network drivers, FatFs, UDP/TCP sockets, DNS, HTTP, NTP, and a 1 MiB TFTP file-transfer test with SHA-256 verification. See [the design and implementation specification](docs/quard-star-riscv64-net-design.md) for the architecture, source baselines, milestones, and acceptance criteria.

First-party migration baselines are recorded in [docs/source-migration.md](docs/source-migration.md); third-party versions and licenses are recorded in [THIRD_PARTY.md](THIRD_PARTY.md).

Build and debugging commands are documented in [docs/build-debug.md](docs/build-debug.md). Current security and protocol boundaries are listed in [docs/limitations.md](docs/limitations.md).

## Release status

The current release is `v1.0.1`. Host CI and the M8 QEMU/TAP acceptance test cover the implemented eight-hart, storage, network, FreeRTOS scheduling, and domain-isolation paths described below.

`v1.0.1` is a maintenance release that adds reproducible performance reports,
strengthens PMP access-fault evidence, updates GitHub Actions, hardens CI and
test-registration contracts, and aligns the post-release documentation. It
does not expand the `v1.0.0` protocol or hardware-support boundary.

`v1.0.0` adds an S-mode FreeRTOS scheduler on hart7 and PMP-enforced memory isolation between the OpenSBI domains. Hart7 receives an 8 MiB trusted RAM region and UART2, while harts 0-6 are denied both regions. See [docs/limitations.md](docs/limitations.md) for the remaining boundaries.

## Verification

Run host tests on Ubuntu 24.04 or 26.04:

```sh
make test-host
```

Build and run the full eight-hart QEMU/TAP acceptance test:

```sh
make m8-build
sudo -v
make m8-smoke
```

The M8 smoke test requires Linux TAP access and verifies harts 0-6, the hart7 FreeRTOS scheduler (`QS:TRUSTED_SCHED_OK`), bidirectional PMP denial (`QS:PMP_UNTRUSTED_DENY_OK` and `QS:PMP_TRUSTED_DENY_OK`), DNS, HTTP, NTP, and a 1 MiB TFTP transfer. It does not require public network access.

GitHub Actions runs host tests on every push and pull request. The full M8 QEMU/TAP smoke test runs weekly and can also be started manually; its serial logs and peer statistics are uploaded as workflow artifacts.

Use [the performance baseline guide](docs/performance-baseline.md) to turn M8
and cumulative TCP stress artifacts into validated reports. Timing remains an
observation, not a cross-host CI threshold.

## Acknowledgements

The kernel's educational design was inspired by Tsinghua University's open-source rCore project. Thanks to the rCore contributors for making operating-system concepts and implementation techniques accessible to learners. The kernel in this repository is an independent C design and implementation.
