# Host CI Submodule Checkout Design

## Problem

The `host-tests` workflow initializes every direct submodule before running
tests. A pull-request run failed before tests started because the upstream DTC
server returned HTTP 502 twice. Host tests exercise first-party sources,
scripts, patches, and generated test fixtures; they do not build the locked
third-party trees.

## Design

The daily `host-tests` workflow will use the default `actions/checkout@v5`
behavior and leave submodules uninitialized. The M8 workflow will continue to
initialize direct submodules because it builds QEMU, OpenSBI, FreeRTOS, and the
kernel firmware.

The M9 workflow contract will require that `host-tests.yml` does not request
submodules and will continue to require `submodules: true` for `m8-smoke.yml`.
No retry wrapper, mirror, new dependency, or source URL will be added.

## Source Integrity

This change does not alter `.gitmodules`, gitlink revisions,
`scripts/check-sources.sh`, or build targets. Firmware builds still reject
missing or mismatched submodules before compiling. Only the host-only CI job is
decoupled from third-party availability.

## Verification

1. Run the focused M9 contract and confirm it fails while the host workflow
   still contains `submodules: true`.
2. Remove that option from `host-tests.yml` and update the contract.
3. Clone the local repository without submodules and run `make test-host` to
   prove the workflow's checkout state is sufficient.
4. Run the complete host suite and `git diff --check` in the feature worktree.

## Boundaries

- M8 checkout and build behavior remain unchanged.
- Host CI does not claim that third-party source trees are fetchable or
  buildable; M8 and explicit build targets provide that evidence.
- The earlier DTC HTTP 502 remains an external service failure, not a product
  defect or performance result.
