# Host Test Python Cache Cleanup Design

## Goal

Keep `make test-host` from creating Python bytecode files in the source tree.

## Design

Apply `PYTHONDONTWRITEBYTECODE=1` to the Python invocation in
`tests/host/test_m7e_peer.sh`. This test imports `scripts/m5-peer.py` and was
the remaining host test without the repository convention. The change only
affects test-process behavior and prevents source-tree cache pollution.

## Alternatives

- `python3 -B`: equivalent behavior, but inconsistent with the existing tests.
- `.gitignore` rules: hide the pollution without preventing it, so rejected.

## Verification

Run the focused peer test, confirm no cache directory is created, then run the
full `make test-host` and `make test-build` targets.
