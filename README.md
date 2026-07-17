# quard-star-riscv64-net

`quard-star-riscv64-net` is an educational RISC-V64 SMP operating system for the custom QEMU quard-star machine. It combines an independently implemented C kernel, OpenSBI domains, a FreeRTOS trusted domain, VirtIO block and network devices, and the author's tiny TCP/IP stack.

The project is currently in the design stage. See [the design and implementation specification](docs/quard-star-riscv64-net-design.md) for the architecture, source baselines, milestones, and acceptance criteria.

First-party migration baselines are recorded in [docs/source-migration.md](docs/source-migration.md); third-party versions and licenses are recorded in [THIRD_PARTY.md](THIRD_PARTY.md).

## Acknowledgements

The kernel's educational design was inspired by Tsinghua University's open-source rCore project. Thanks to the rCore contributors for making operating-system concepts and implementation techniques accessible to learners. The kernel in this repository is an independent C design and implementation.
