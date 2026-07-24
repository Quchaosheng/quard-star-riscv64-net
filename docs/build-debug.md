# Build and Debug

## Environment

Use Ubuntu 24.04 or 26.04, either directly or through WSL2. Check the required commands and development headers before building:

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

Build and run the full system test:

```sh
make m8-build
sudo -v
make m8-smoke
```

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
