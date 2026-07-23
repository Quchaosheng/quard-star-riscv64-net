# Performance Baseline Design

## Goal

Turn the existing M8 and M6C2 stress artifacts into a reproducible baseline
report without changing kernel behavior or treating hosted-runner timing as a
stable performance contract.

## Scope

The baseline tool reads artifacts that the current acceptance tests already
produce:

- `qemu.log`, for guest tick counters and pass markers;
- `m5-peer.stats`, for host elapsed time and protocol counters;
- Git metadata, when available, for the measured commit.

It writes one JSON report for machine processing and one Markdown report for
human review. The first checked report records the existing local M8 and M6C2
stress runs as historical observations, not as universal expected values.

This work does not add kernel probes, change protocol code, introduce a new
dependency, or fail CI when timing changes.

## Command Interface

The reporter is a Python standard-library program:

```text
python3 scripts/perf-baseline.py \
  --stage m6c2-stress \
  --commit bfec50552149916e498d5919607e81c27bd08369 \
  --qemu-log out/m6c2-stress/qemu.log \
  --peer-stats out/m6c2-stress/m5-peer.stats \
  --json-out out/performance/m6c2-stress.json \
  --markdown-out out/performance/m6c2-stress.md
```

All paths are explicit so the tool can process local runs or downloaded CI
artifacts. `--stage` is recorded verbatim and is not inferred from a path.
`--commit` is optional and takes precedence over the current Git HEAD; callers
processing downloaded artifacts must supply it to avoid false attribution.

## Report Schema

The JSON document contains:

- `schema_version`: initially `1`;
- `stage`: the caller-supplied test stage;
- `commit`: explicit `--commit`, otherwise `git rev-parse HEAD` when available,
  otherwise `null`;
- `guest`: allocation operations, migrations, elapsed ticks, and final pass
  marker parsed from `QS:` log lines;
- `peer`: the complete validated integer/number counter map from
  `m5-peer.stats`;
- `derived`: only dimensionally safe values, initially
  `tftp_bytes_per_second` when both byte and elapsed fields exist;
- `interpretation`: a fixed warning that results from different hosts or QEMU
  configurations must not be compared directly.

Unknown peer counters are preserved so the reporter does not need a code
change each time the peer adds evidence. Required fields remain stage-specific:

- every report requires `elapsed_seconds`, a positive guest elapsed tick count,
  and a `QS:TEST_PASS:` marker;
- `m8` requires exactly 1 MiB of TFTP bytes and zero TFTP outstanding packets;
- `m6c2-stress` requires 108 handshakes/echoes/FINs, peak parallelism 8,
  100 reconnects, and zero outstanding/live connections.

## Validation And Errors

Malformed JSON, non-numeric counters, missing files, absent pass markers,
non-positive elapsed values, or stage-specific counter mismatches cause a
nonzero exit with one concise error on stderr. The tool never silently emits a
partial report.

Markdown output is rendered only after the complete JSON model validates. Both
documents are rendered before any output is touched, then each destination is
replaced through a temporary sibling file. Each file is atomic; the pair is not
claimed to be a cross-file transaction.

## Tests

A host test creates temporary synthetic M8 and M6C2 artifacts and verifies:

- valid input produces deterministic JSON and Markdown;
- M8 and stress counters are enforced;
- malformed JSON and missing markers fail;
- an unavailable Git repository yields `commit: null` rather than failure;
- a failed invocation leaves existing output files unchanged.

The test is added to `make test-host`. The complete host suite remains the
regression gate.

## Documentation

`docs/performance-baseline.md` explains how to run the two existing workloads,
generate reports, and interpret the measurements. It records the first local
observations only with their commit, date, stage, and environment limitations.
No optimization claim is made until the same workload is repeated on the same
host and demonstrates a consistent change.

## Non-Goals

- No throughput or latency target is enforced in CI.
- No comparison is made between local Windows/WSL runs and GitHub runners.
- No scheduler, allocator, VirtIO, Socket, or TCP implementation is changed.
- No benchmark database, dashboard, plotting library, or new workflow is added.
- No result is described as physical-hardware performance.
