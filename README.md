# quard-star-riscv64-net

`quard-star-riscv64-net` is an educational RISC-V64 SMP operating system for the custom QEMU quard-star machine. It combines an independently implemented C kernel, OpenSBI domains, a dedicated FreeRTOS trusted hart, VirtIO block and network devices, and the author's tiny TCP/IP stack.

The implementation currently boots seven ordinary SMP harts and one isolated FreeRTOS trusted hart. The kernel includes VirtIO block/network drivers, FatFs, UDP/TCP sockets, DNS, HTTP, NTP, and a 1 MiB TFTP file-transfer test with SHA-256 verification. See [the design and implementation specification](docs/quard-star-riscv64-net-design.md) for the architecture, source baselines, milestones, and acceptance criteria.

First-party migration baselines are recorded in [docs/source-migration.md](docs/source-migration.md); third-party versions and licenses are recorded in [THIRD_PARTY.md](THIRD_PARTY.md).

Build and debugging commands are documented in [docs/build-debug.md](docs/build-debug.md). Current security and protocol boundaries are listed in [docs/limitations.md](docs/limitations.md).

## Release status

The current release line is `v0.9.0`. Host CI and the M8 QEMU/TAP acceptance test cover the implemented eight-hart, storage, and network paths described below.

`v1.0.0` remains blocked on PMP-enforced memory isolation between the OpenSBI domains. The current domain configuration proves hart ownership and independent startup, but both domains still use `allmem`; see [docs/limitations.md](docs/limitations.md).

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

The M8 smoke test requires Linux TAP access and verifies harts 0-6, the trusted hart7 UART marker, DNS, HTTP, NTP, and a 1 MiB TFTP transfer. It does not require public network access.

GitHub Actions runs host tests on every push and pull request. The full M8 QEMU/TAP smoke test runs weekly and can also be started manually; its serial logs and peer statistics are uploaded as workflow artifacts.

## Acknowledgements

The kernel's educational design was inspired by Tsinghua University's open-source rCore project. Thanks to the rCore contributors for making operating-system concepts and implementation techniques accessible to learners. The kernel in this repository is an independent C design and implementation.
