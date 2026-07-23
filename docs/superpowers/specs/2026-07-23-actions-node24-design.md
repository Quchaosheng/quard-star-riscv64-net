# GitHub Actions Node.js 24 Upgrade Design

## Problem

The successful M8 run on merge commit `50f447e` reports that
`actions/cache@v4` and `actions/upload-artifact@v4` target deprecated Node.js
20 and are being forced onto Node.js 24 by GitHub-hosted runners. The workflow
still passes, but the warning identifies a maintenance boundary that should be
resolved before it becomes an execution failure.

## Scope

- Replace `actions/cache@v4` with `actions/cache@v6`.
- Replace `actions/upload-artifact@v4` with
  `actions/upload-artifact@v7`.
- Update the M9 workflow contract to require those major versions.
- Preserve the cache path, cache key, artifact name, artifact paths, and
  `if-no-files-found` behavior.

Both selected action majors declare `runs.using: node24`. Cache v6 requires
Actions Runner 2.327.1 or newer; this repository uses GitHub-hosted
`ubuntu-24.04`, not a self-hosted runner.

## Non-Goals

- No kernel, network stack, protocol, build, or QEMU behavior changes.
- No change to `actions/checkout@v5`, which was not named in the warning.
- No change from the repository's existing major-tag action policy to pinned
  commit SHAs.
- No release-number change.

## Contract And Verification

The M9 source contract will first be changed to require `actions/cache@v6`
and `actions/upload-artifact@v7`; it must fail against the current workflow.
After the workflow references are upgraded, M9 and the complete no-submodule
host suite must pass.

The final acceptance check is a manual M8 workflow run on the feature branch.
It must complete the QEMU/TAP smoke, the post-smoke `make test-build` step, and
the serial-log upload without the Node.js 20 deprecation annotation. The
artifact must retain the existing kernel and trusted acceptance markers.
