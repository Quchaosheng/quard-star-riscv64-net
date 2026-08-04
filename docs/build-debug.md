# Build and Debug

**English** | [简体中文](build-debug.zh-CN.md)

## Environment

Use Ubuntu 24.04 or 26.04, either directly or through WSL2. Before building,
`make check-env` validates the supported Ubuntu version, required commands,
the `glib-2.0` and `pixman-1` pkg-config entries, and linkable `libfdt`
development files. It does not install packages or initialize submodules.

```sh
make check-env
make deps
```

`make deps` initializes the locked submodules and downloads the FatFs archive recorded in `third_party/fatfs.lock`.

## Tests

Run host tests without QEMU or TAP:

```sh
make test-host
```

Build and run the source-tied full system acceptance test:

```sh
make m8-build
sudo -E make m8-smoke
```

`sudo -E make m8-smoke` is the direct command used by CI after the build.
`make run` requires cached artifacts produced by `make m8-build`; it checks
only that cache, does not rebuild, and delegates to `m8-smoke` with `sudo`. It
does not establish that the cached files came from the current worktree or
`HEAD`, so use the build-plus-direct-smoke sequence when the source itself is
the subject of the claim.

The smoke test creates `tap0`, assigns `192.168.100.1/24`, starts a local raw-packet peer, and removes the TAP device on exit. It does not use public DNS or Internet services.

## Logs

M8 output is stored under `out/m8`:

- `kernel.log`: OpenSBI and ordinary kernel UART output.
- `trusted.log`: hart7 FreeRTOS UART output.
- `qemu.err`: QEMU diagnostics.
- `m5-peer.stats`: observed TAP exchange counters.

Successful runs contain `QS:TEST_PASS:m8-smoke` and `QS:PMP_UNTRUSTED_DENY_OK` in `kernel.log`. `trusted.log` must contain `QS:TRUSTED_READY`, `QS:TRUSTED_SCHED_OK`, and `QS:PMP_TRUSTED_DENY_OK`. A `QS:TEST_FAIL` marker takes precedence over later output.

## Kernel Debugging

The kernel and trusted firmware retain ELF files with symbols:

```sh
gdb-multiarch kernel/os.elf
gdb-multiarch trusted/build/trusted_fw.elf
```

For protocol failures, inspect the first missing stable marker and `m5-peer.stats` before changing timeouts. Rebuild M7E or M8 from a fresh generated disk image when investigating FatFs allocation failures.
