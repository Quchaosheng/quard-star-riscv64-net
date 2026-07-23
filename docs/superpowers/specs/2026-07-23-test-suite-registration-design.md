# Test Suite Registration Design

## Problem

The repository contains 84 `tests/host/test_*.sh` scripts, but the Makefile
does not invoke `test_m3_contracts.sh` or `test_m1_build_contracts.sh`. The M3
script is a source-only contract and passes with all submodules uninitialized.
The M1 script performs real user and kernel builds and checks generated ELF
segments, so it correctly fails without DTC/libfdt and complete build outputs.

Leaving either script unregistered allows its checks to become stale without
CI reporting a failure.

## Design

`make test-host` will invoke `test_m3_contracts.sh` with the other M3 host
checks. A new `make test-build` target will invoke
`test_m1_build_contracts.sh`; it will not be part of the no-submodule host
suite.

The M8 workflow will run `make test-build` only after the QEMU/TAP smoke test
passes. Running it afterward prevents the build contract's clean rebuild from
changing firmware before M8 acceptance. The M8 job already initializes the
locked submodules and produces the trusted firmware ELF required by the test.

Registering the build contract exposed a latent default-build defect:
`task.c` referenced M7E file-handle cleanup even when the M7E file subsystem
was not compiled. The cleanup call will use the same `QS_M7E_TEST` feature gate
as file initialization and file syscalls. M7E and M8 keep process-exit cleanup;
the default `FATFS=0` kernel no longer references an omitted implementation.

## Registration Contract

The M0 script behavior test will dry-run `test-host` and `test-build`, then
require every tracked `tests/host/test_*.sh` command to appear in that output.
This proves each script is reachable from a CI entry target without assuming
that every script belongs to `test-host`.

The M9 workflow contract will extract the single `qemu-smoke` job, require the
smoke and build-contract commands exactly once within that job, and verify that
`sudo -E make m8-smoke` appears before `make test-build`. Existing checks for
the M8 build, smoke, cache, and artifacts remain unchanged.

## Verification

1. Add the registration contract and verify that it reports an omitted test
   script.
2. Register M3 in `test-host` and M1 in `test-build`, then verify the
   registration contract passes.
3. Add the M8 ordering contract and verify it fails before the workflow step
   exists.
4. Add the post-smoke `make test-build` workflow step and verify M9 passes.
5. Run `make test-host` in a no-submodule clone.
6. Run `make test-build` in the prepared worktree containing complete M8 build
   artifacts.
7. Verify the default build fails before the M7E cleanup gate is restored and
   passes afterward; run focused M7E and M8 contracts.
8. Run `git diff --check` and the complete host suite.

## Boundaries

- The only kernel change restores the existing M7E feature gate around
  process-exit file cleanup. Protocol, platform, and third-party sources remain
  unchanged.
- Push and pull-request host CI remains independent of submodule downloads.
- The build contract runs with weekly or manually dispatched M8 acceptance;
  it is not described as per-commit evidence.
- `test-build` requires initialized locked submodules and prior complete
  firmware build outputs. It fails rather than fetching dependencies itself.
