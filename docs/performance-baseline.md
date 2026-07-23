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

```sh
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

The first traceable observation is recorded only from fresh runs made after the
reporter was added. Earlier local artifacts remain acceptance evidence but are
not attributed to a commit after the fact.
