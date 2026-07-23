# Performance Baselines

Performance work starts from repeatable acceptance workloads. These reports are
observations, not CI gates: values from different hosts, QEMU builds, or runner
types must not be compared directly.

## Workloads

### M8 integration

M8 boots seven ordinary harts and one trusted FreeRTOS hart, exercises the
storage and network application chain, transfers a 1 MiB file over TFTP, and
checks bidirectional PMP denial. Its peer elapsed time covers the integrated
acceptance path rather than one isolated protocol operation.

The reporter validates the ordinary QEMU log and TAP-peer counters. It does not
read `trusted.log`; trusted scheduling and trusted-side PMP markers remain the
responsibility of the complete `m8-smoke.sh` acceptance check.

```sh
./scripts/prepare-fatfs.sh
make m8-build
sudo -v
sudo -E make m8-smoke
commit=$(git rev-parse HEAD)
python3 scripts/perf-baseline.py \
  --stage m8 --commit "$commit" \
  --qemu-log out/m8/qemu.log \
  --peer-stats out/m8/m5-peer.stats \
  --json-out out/performance/m8.json \
  --markdown-out out/performance/m8.md
```

`m8-smoke.sh` modifies its generated FatFs image. Re-run `make m8-build` before
every M8 sample so that each measurement starts from a newly generated disk.
Reusing `out/m8/disk/disk.img` across samples does not produce a comparable
sample for this fresh-disk baseline.

When running inside a Windows-created Git worktree from WSL2, use
`git.exe rev-parse HEAD | tr -d '\r'` to obtain the commit because the worktree
metadata contains a Windows path.

### M6C2 cumulative stress

The stress workload performs 100000 allocator operations, 10000 migrations,
eight parallel TCP server connections, and 100 reconnects. It must finish with
108 completed handshakes, echoes, and FINs and no live or outstanding
connections.

```sh
sudo -E make m6c2-stress
commit=$(git rev-parse HEAD)
python3 scripts/perf-baseline.py \
  --stage m6c2-stress --commit "$commit" \
  --qemu-log out/m6c2-stress/qemu.log \
  --peer-stats out/m6c2-stress/m5-peer.stats \
  --json-out out/performance/m6c2-stress.json \
  --markdown-out out/performance/m6c2-stress.md
```

## Metrics

| Metric | Source | Meaning |
|---|---|---|
| Guest elapsed ticks | `QS:STRESS_ELAPSED_TICKS` | Guest `mtime` ticks from self-test start to the final gate |
| Host elapsed seconds | `m5-peer.stats` | Wall time observed by the TAP peer |
| Allocation operations | QEMU log | Completed allocator stress operations across ordinary harts |
| Migrations | QEMU log | Completed cross-hart scheduler migrations |
| TFTP bytes | Peer stats | Application payload accepted by the 1 MiB transfer |
| TCP stress counters | Peer stats | Completed connections, peak concurrency, reconnects, and cleanup state |

`tftp_bytes_per_second` is derived only from TFTP bytes divided by peer elapsed
seconds. It describes the complete M8 peer interval and is not wire-speed TFTP
throughput.

## Comparison Rules

1. Compare only repeated runs using the same host, WSL distribution, QEMU
   build, command, and workload configuration.
2. Keep raw `qemu.log`, `m5-peer.stats`, and generated JSON with the result.
3. Run at least three samples before claiming an improvement; report the
   individual values rather than only the best result.
4. Treat GitHub hosted-runner values as execution evidence, not a regression
   threshold.
5. Do not describe QEMU results as physical-board performance.

## Initial Observation

These first observations were captured on 2026-07-23 in WSL2 Ubuntu 24.04 on
the same Windows 11 host. They establish traceable starting points only; one
sample cannot demonstrate an optimization.

| Stage | Commit | Guest elapsed ticks | Host elapsed seconds | Stage evidence |
|---|---|---:|---:|---|
| M8 | `7eeadb1e9689732d96a3474089966b1041559ae0` | 264957814 | 30.02252233500002 | Reporter validated 14000 allocations, 100 migrations, 1 MiB TFTP and zero outstanding TFTP packets; the complete smoke separately passed trusted/PMP checks |
| M6C2 stress | `cfb695f6a4940769d01ae1c7e331c71fd530610a` | 3225305239 | 245.72794389 | 100000 allocations, 10000 migrations, 108 TCP exchanges, peak 8, 100 reconnects, zero live/outstanding connections |

The M8 integrated interval derives 34926.31 TFTP payload bytes per second. It
includes boot and the other acceptance operations, so it must not be presented
as isolated network throughput. Earlier local artifacts remain acceptance
evidence but are not attributed to a commit after the fact.

## Repeatability Observation

Three fresh samples per workload were captured on 2026-07-23 on the same WSL2
Ubuntu 24.04 and Windows 11 host at commit
`080a9dd40de6a8ff1aae66bfa80a8bb315e39f6d`. These measurements characterize
the current baseline; they do not demonstrate an optimization.

The host description and fresh-disk procedure are operator-recorded run
metadata. The current JSON schema validates the commit, workload counters, pass
marker, and elapsed values, but does not encode or independently verify the
host identity, QEMU binary hash, build command, or initial disk hash. The
reporter records a syntactically validated commit identifier; it does not prove
that the supplied logs were produced by that commit.

| Stage | Sample | Guest elapsed ticks | Host elapsed seconds |
|---|---:|---:|---:|
| M8 | 1 | 248036291 | 31.84657113899999 |
| M8 | 2 | 258244754 | 29.602071138 |
| M8 | 3 | 254639209 | 28.443526686 |
| M6C2 stress | 1 | 3229017703 | 249.91188124800001 |
| M6C2 stress | 2 | 3222401076 | 249.816960978 |
| M6C2 stress | 3 | 3220955776 | 247.64832866899997 |

Relative spread is `(maximum - minimum) / median * 100`.

| Stage | Metric | Minimum | Median | Maximum | Relative spread |
|---|---|---:|---:|---:|---:|
| M8 | Guest elapsed ticks | 248036291 | 254639209 | 258244754 | 4.009% |
| M8 | Host elapsed seconds | 28.443526686 | 29.602071138 | 31.84657113899999 | 11.496% |
| M6C2 stress | Guest elapsed ticks | 3220955776 | 3222401076 | 3229017703 | 0.250% |
| M6C2 stress | Host elapsed seconds | 247.64832866899997 | 249.816960978 | 249.91188124800001 | 0.906% |

According to the recorded run procedure, every included M8 sample rebuilt the
generated disk before the smoke run. An earlier attempt reused the disk from
the preceding sample and failed the M7E TFTP RRQ acceptance check. That attempt
is not comparable with this fresh-disk baseline and is excluded from the
performance samples above.
