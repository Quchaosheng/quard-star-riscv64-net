# Host Test Python Cache Cleanup Design

## Goal

Keep `make test-host` from creating Python bytecode files in the source tree.

## Design

Apply `PYTHONDONTWRITEBYTECODE=1` to every Python invocation in
`tests/host/test_performance_baseline.sh`. This matches the existing peer-test
convention and changes only test-process behavior. Add a regression assertion
that the test does not leave `scripts/__pycache__` behind.

## Alternatives

- `python3 -B`: equivalent behavior, but inconsistent with the existing tests.
- `.gitignore` rules: hide the pollution without preventing it, so rejected.

## Verification

Run the focused host test, confirm no cache directory is created, then run the
full `make test-host` and `make test-build` targets.
