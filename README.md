# quard-star-riscv64-net

[![host-tests](https://github.com/Quchaosheng/quard-star-riscv64-net/actions/workflows/host-tests.yml/badge.svg)](https://github.com/Quchaosheng/quard-star-riscv64-net/actions/workflows/host-tests.yml)
[![m8-smoke](https://github.com/Quchaosheng/quard-star-riscv64-net/actions/workflows/m8-smoke.yml/badge.svg)](https://github.com/Quchaosheng/quard-star-riscv64-net/actions/workflows/m8-smoke.yml)
[![release](https://img.shields.io/github/v/release/Quchaosheng/quard-star-riscv64-net)](https://github.com/Quchaosheng/quard-star-riscv64-net/releases/latest)
[![license](https://img.shields.io/github/license/Quchaosheng/quard-star-riscv64-net)](LICENSE)

An educational RISC-V64 SMP operating system for the custom QEMU quard-star
machine. The project combines an independently implemented C kernel, OpenSBI
domains, a dedicated FreeRTOS trusted hart, VirtIO block and network devices,
FatFs, and a small first-party TCP/IP stack.

The complete M8 system boots seven ordinary kernel harts and one isolated
FreeRTOS hart. Its deterministic QEMU/TAP acceptance test covers SMP,
storage, networking, application protocols, trusted scheduling, and
PMP-enforced memory isolation without depending on the public Internet.

## QEMU demo

[![Watch the 42-second M8 QEMU/TAP evidence replay](docs/assets/qemu-m8-demo-poster.png)](docs/assets/qemu-m8-demo.mp4)

The 42-second video is generated from a real, passing M8 run. Every displayed
result is validated against `qemu.log`, `trusted.log`, and `m5-peer.stats`
before rendering; it is an evidence replay rather than a hand-authored success
animation. The accompanying [evidence manifest](docs/assets/qemu-m8-demo-evidence.json)
records the source and media SHA-256 digests.

Reproduce the complete run and video on Ubuntu 24.04/26.04 or WSL2:

```sh
make demo
```

To render again from an already accepted `out/m8` artifact set, use
`make demo-render`. See [QEMU Demo](docs/qemu-demo.md) for requirements,
validation rules, and generated files. Scheduled and release-tag M8 workflows
also regenerate the media and include it in the `m8-serial-logs` artifact.

## At a glance

| Area | Implemented scope |
| --- | --- |
| CPU | RISC-V64, harts 0-6 in one SMP kernel, hart 7 in a separate FreeRTOS domain |
| Firmware | OpenSBI HSM, TIME, IPI, domain configuration, and PMP isolation |
| Kernel | Sv39, per-hart state, scheduler, migration, traps, timers, syscalls, and synchronization |
| Storage | Shared VirtIO MMIO/virtqueue layer, VirtIO block, FatFs, and generation-checked file handles |
| Networking | VirtIO net, Ethernet, ARP, IPv4, ICMP, UDP, TCP, loopback, and sockets |
| Applications | Ping, UDP/TCP echo, DNS, HTTP, NTP, and TFTP with a 1 MiB SHA-256-verified transfer |
| Trusted runtime | FreeRTOS S-mode scheduler on hart 7, trusted RAM, UART2, and SBI timer ticks |
| Verification | Host tests, QEMU/TAP smoke tests, stable serial markers, stress tests, and performance reports |

## System architecture

```mermaid
flowchart TB
    QEMU["QEMU quard-star machine<br/>8 RISC-V64 harts"] --> SBI["OpenSBI<br/>HSM, TIME, IPI, domains, PMP"]

    subgraph UNTRUSTED["Untrusted domain"]
        HARTS["harts 0-6"] --> KERNEL["SMP C kernel"]
        KERNEL --> CORE["Sv39, scheduler, traps,<br/>timers, syscalls"]
        KERNEL --> STORAGE["VirtIO block + FatFs"]
        KERNEL --> NET["VirtIO net + TCP/IP stack"]
        CORE --> APPS["User applications"]
        APPS --> NET
        APPS --> STORAGE
    end

    subgraph TRUSTED["Trusted domain"]
        HART7["hart 7"] --> RTOS["FreeRTOS S-mode runtime"]
        RTOS --> TUART["UART2 polling log"]
        RTOS --> TTIME["SBI TIME scheduler ticks"]
    end

    SBI --> HARTS
    SBI --> HART7
    STORAGE --> VBLK["VirtIO block MMIO"]
    NET --> VNET["VirtIO net MMIO"]
    VNET <--> TAP["Linux TAP<br/>192.168.100.0/24"]

    PMP["PMP boundary"] -. "denies trusted RAM and UART2" .-> UNTRUSTED
    PMP -. "denies ordinary RAM and devices" .-> TRUSTED
```

OpenSBI gives the ordinary kernel and trusted runtime different CPU and device
views. Hart 7 never joins the kernel allocator, scheduler, locks, interrupt
routing, or network stack. The current security evidence is QEMU-only; see
[Current Limitations](docs/limitations.md) for the exact claim boundary.

### Boot and isolation flow

```mermaid
sequenceDiagram
    participant Q as QEMU
    participant S as OpenSBI
    participant B as hart 0
    participant C as harts 1-6
    participant T as hart 7

    Q->>S: Reset all eight harts
    S->>S: Parse domain DTB and install PMP rules
    S->>B: Enter ordinary kernel with kernel DTB
    S->>T: Enter trusted FreeRTOS image
    B->>B: Initialize memory, traps, PLIC, and scheduler
    loop Each secondary ordinary hart
        B->>S: SBI HSM hart_start
        S->>C: Enter secondary boot path
        C-->>B: Publish online state
    end
    T->>S: Program scheduler ticks with SBI TIME
    T-->>T: Start the FreeRTOS scheduler
    B-->>B: Probe trusted RAM and UART2 denial
    T-->>T: Probe ordinary RAM denial
```

M8 requires load, store, and instruction faults in both directions. The summary
markers are `QS:PMP_UNTRUSTED_DENY_OK` and `QS:PMP_TRUSTED_DENY_OK`; trusted
scheduling is accepted only after `QS:TRUSTED_SCHED_OK` appears on UART2.

## Data paths

### Socket and network flow

TCP/IP protocol state is owned by one network core thread. Processes may run on
any ordinary hart, but socket operations cross a request queue before mutating
network state.

```mermaid
flowchart LR
    APP["Process on hart 0-6"] --> SYSCALL["Socket syscall"]
    SYSCALL --> REQ["Network request queue"]
    REQ --> EXEC["Serialized network core"]
    EXEC --> SOCK["Socket layer"]
    SOCK --> L4["UDP or TCP"]
    L4 --> IP["IPv4 + ICMP"]
    IP --> L2["ARP + Ethernet"]
    L2 --> NETIF["VirtIO netif"]
    NETIF <--> RING["RX/TX virtqueues"]
    RING <--> DEVICE["QEMU VirtIO net"]
    DEVICE <--> TAP["tap0"]
    TAP <--> PEER["Deterministic local peer<br/>DNS, HTTP, NTP, TFTP, echo"]
    EXEC --> WAKE["Completion and wakeup"]
    WAKE --> APP
```

### Storage and file-transfer flow

```mermaid
flowchart LR
    DISK["Generated disk image"] <--> VBLK["QEMU VirtIO block"]
    VBLK <--> VQ["Shared virtqueue layer"]
    VQ <--> BIO["Block I/O"]
    BIO <--> FAT["FatFs"]
    FAT <--> FILE["Kernel file API"]

    SERVER["Local TFTP server<br/>1 MiB fixture"] --> TAP["TAP peer"]
    TAP --> VNET["VirtIO net"]
    VNET --> UDP["IPv4 + UDP + TFTP"]
    UDP --> HASH["SHA-256 verification"]
    HASH --> FILE
```

The M8 build always regenerates its disk image. This prevents a stale or full
image from changing the result of the TFTP and FatFs acceptance path.

## Quick start

### Supported environment

- Ubuntu 24.04 or 26.04, directly or through WSL2.
- A RISC-V bare-metal GCC/binutils toolchain.
- Build tools and development headers checked by `make check-env`.
- Linux TUN/TAP permissions for QEMU end-to-end tests.

Native Windows networking is not an acceptance environment. Use WSL2 when the
repository is hosted on Windows.

### Prepare dependencies

```sh
git clone --recurse-submodules \
  https://github.com/Quchaosheng/quard-star-riscv64-net.git
cd quard-star-riscv64-net

make check-env
make deps
```

`make deps` initializes the locked direct submodules and retrieves the FatFs
archive recorded in `third_party/fatfs.lock`.

### Run host tests

```sh
make test-host
```

Host tests do not boot QEMU or create a TAP interface. They cover contracts,
parsers, queues, protocol behavior, socket lifecycle, scripts, and CI policy.

### Build and run the complete system

```sh
make m8-build
sudo -v
make m8-smoke
```

The smoke test creates and configures `tap0`, starts deterministic local
protocol peers, boots the full eight-hart machine, checks all stable markers,
and removes its networking resources on exit. Public DNS and Internet access
are not required.

### Inspect results

M8 writes its evidence under `out/m8`:

| File | Contents |
| --- | --- |
| `kernel.log` | OpenSBI and ordinary kernel UART output |
| `trusted.log` | FreeRTOS hart 7 UART2 output |
| `qemu.log` | Combined smoke-test console capture |
| `qemu.err` | QEMU diagnostics |
| `m5-peer.stats` | TAP peer counters and transfer observations |

A successful run contains `QS:TEST_PASS:m8-smoke`. A `QS:TEST_FAIL` marker
takes precedence over any later output. For debugging commands and marker
interpretation, use [Build and Debug](docs/build-debug.md).

## Acceptance flow

```mermaid
flowchart TD
    START["make m8-smoke"] --> TAP["Create TAP and local protocol peer"]
    TAP --> BOOT["Boot patched QEMU quard-star machine"]
    BOOT --> SMP{"harts 0-6 online?"}
    SMP -- No --> FAIL["Fail and preserve logs"]
    SMP -- Yes --> TRUST{"FreeRTOS ready and scheduled?"}
    TRUST -- No --> FAIL
    TRUST -- Yes --> PMP{"Bidirectional PMP probes pass?"}
    PMP -- No --> FAIL
    PMP -- Yes --> IO["VirtIO block, FatFs, and network checks"]
    IO --> PROTO["ICMP, UDP/TCP, DNS, HTTP, NTP, TFTP"]
    PROTO --> HASH{"1 MiB TFTP SHA-256 matches?"}
    HASH -- No --> FAIL
    HASH -- Yes --> PASS["QS:TEST_PASS:m8-smoke"]
    PASS --> CLEAN["Collect evidence and remove TAP"]
    FAIL --> CLEAN
```

| Gate | Required evidence |
| --- | --- |
| SMP | `QS:HART_ONLINE:0` through `QS:HART_ONLINE:6` |
| Trusted boot | `QS:TRUSTED_READY` |
| Trusted scheduling | `QS:TRUSTED_SCHED_OK` after scheduler ticks |
| Ordinary-domain denial | `QS:PMP_UNTRUSTED_DENY_OK` plus load/store/execute markers |
| Trusted-domain denial | `QS:PMP_TRUSTED_DENY_OK` plus load/store/execute markers |
| File transfer | `QS:M7E_TFTP_1M_OK` and peer counters with no outstanding packets |
| Overall result | `QS:TEST_PASS:m8-smoke` and no `QS:TEST_FAIL` |

## CI and release flow

```mermaid
flowchart TD
    CHANGE["Push or pull request"] --> HOST["host-tests<br/>Ubuntu 24.04"]
    HOST --> UNIT["make test-host"]

    TAG["Push tag v*"] --> TAGHOST["host-tests"]
    TAG --> M8["m8-smoke workflow"]
    WEEKLY["Weekly schedule"] --> M8
    MANUAL["Manual dispatch"] --> M8

    M8 --> CHECKOUT["Checkout direct submodules"]
    CHECKOUT --> DEPS["Install tools, restore cache, prepare FatFs"]
    DEPS --> BUILD["make m8-build"]
    BUILD --> SMOKE["sudo -E make m8-smoke"]
    SMOKE --> CONTRACTS["make test-build"]
    CONTRACTS --> ARTIFACT["Upload m8-serial-logs<br/>even when a prior step fails"]

    UNIT --> REVIEW["Merge decision"]
    TAGHOST --> RELEASE["Release evidence"]
    ARTIFACT --> RELEASE
```

GitHub Actions runs host tests on every push and pull request. M8 runs weekly,
on manual dispatch, and automatically for every `v*` release tag. Tag releases
therefore have both fast host-test evidence and full QEMU/TAP evidence at the
exact tagged commit.

## Implementation milestones

```mermaid
flowchart LR
    M0["M0<br/>Repository baseline"] --> M1["M1<br/>Single-hart boot"]
    M1 --> M2["M2<br/>SMP and stress"]
    M2 --> M3["M3<br/>Shared VirtIO"]
    M3 --> M4["M4<br/>VirtIO net + TAP"]
    M4 --> M5["M5<br/>IPv4 + ping"]
    M5 --> M6["M6<br/>Sockets, UDP, TCP"]
    M6 --> M7["M7<br/>DNS, HTTP, NTP, TFTP"]
    M7 --> M8["M8<br/>7 + 1 harts"]
    M8 --> M9["M9<br/>CI, docs, release"]
```

The staged `m1-*` through `m8-*` scripts remain available for focused
regression and fault isolation. The current release target is the cumulative
M8 configuration.

## Repository map

| Path | Responsibility |
| --- | --- |
| `kernel/` | SMP kernel, drivers, FatFs port, file layer, and TCP/IP stack |
| `user/` | User applications linked into the test image |
| `trusted/` | FreeRTOS S-mode platform, port, scheduler demo, and UART2 driver |
| `platform/quard-star/` | Shared memory map, boot assets, and domain/kernel DTS files |
| `scripts/` | Environment, dependency, staged build, smoke, stress, TAP, and report tooling |
| `tests/host/` | Host unit, behavior, script, and contract tests |
| `.github/workflows/` | Fast host CI and full M8 QEMU/TAP CI |
| `third_party/` | Pinned upstream submodules and the locked FatFs source archive |
| `patches/` | Project-owned QEMU and OpenSBI integration patches |
| `docs/` | Design, build, migration, limitations, and performance documentation |

Third-party versions and licenses are recorded in [THIRD_PARTY.md](THIRD_PARTY.md).
First-party migration baselines are recorded in
[Source Migration](docs/source-migration.md).

## Performance reports

Validated reports can be generated from M8 integration artifacts or cumulative
M6C2 TCP stress artifacts:

```sh
python3 scripts/perf-baseline.py --help
```

The reporter accepts only the supported `m8` and `m6c2-stress` stages and
updates its JSON and Markdown outputs transactionally. Timing is an observation
for comparable environments, not a cross-host CI pass threshold. See
[Performance Baselines](docs/performance-baseline.md) for collection and
comparison rules.

## Release status

The current release is `v1.0.2`. It is verified by host CI and by the M8
QEMU/TAP acceptance workflow at the exact release commit.

`v1.0.2` is a maintenance release that makes release tags run the full M8
acceptance workflow and hardens performance-report validation and output
rollback. It retains the `v1.0.1` maintenance work and does not expand the
`v1.0.0` protocol or hardware-support boundary.

`v1.0.0` introduced the S-mode FreeRTOS scheduler on hart 7 and bidirectional
PMP isolation between OpenSBI domains. Hart 7 receives an 8 MiB trusted RAM
region and UART2; harts 0-6 are denied both resources.

## Current boundaries

- IPv4 only: no IPv6, DHCP, TLS, HTTPS, or network offloads.
- Fixed `192.168.100.0/24` acceptance network; public connectivity is optional.
- The TFTP implementation covers the tested read path and `windowsize=4`.
- The file layer is a small FatFs test interface, not a POSIX VFS.
- Isolation results cover the QEMU model, not physical hardware, DMA, or side channels.
- Native Windows TAP networking is outside the acceptance environment.

Read [Current Limitations](docs/limitations.md) before extending the security or
protocol claims.

## Documentation

| Document | Use it for |
| --- | --- |
| [Design and Implementation](docs/quard-star-riscv64-net-design.md) | Detailed architecture, resource contracts, milestones, and acceptance criteria |
| [Build and Debug](docs/build-debug.md) | Environment setup, commands, logs, GDB, and failure triage |
| [QEMU Demo](docs/qemu-demo.md) | Reproduce or re-render the verified M8 video |
| [Current Limitations](docs/limitations.md) | Security, protocol, platform, and testing boundaries |
| [Performance Baselines](docs/performance-baseline.md) | Artifact reporting and comparison rules |
| [Source Migration](docs/source-migration.md) | First-party code provenance and migration baselines |
| [Third-Party Inventory](THIRD_PARTY.md) | Dependency revisions and licenses |

## Acknowledgements

The kernel's educational design was inspired by Tsinghua University's
open-source rCore project. Thanks to the rCore contributors for making
operating-system concepts and implementation techniques accessible to learners.
The kernel in this repository is an independent C design and implementation.

Project-owned code is distributed under the repository [MIT License](LICENSE).
Bundled third-party components retain their upstream licenses.
