# M7C NTP Client Implementation Plan

## Task 1: NTP codec

- Add a host-testable request encoder, response parser, and NTP-to-Unix
  timestamp conversion.
- Cover malformed fields and timestamp mismatch under sanitizers.

## Task 2: Guest UDP probe

- Add `user/m7c_ntp_get.c` after M7B.
- Query the local peer, validate a fixed Unix timestamp, then exercise timeout.
- Add M7C build flags and cumulative smoke markers.

## Task 3: TAP peer and verification

- Add `--require-ntp`, deterministic response bytes, counters, and malformed
  query tests to `m5-peer.py`.
- Register host tests, run full regression, build, and real QEMU/TAP smoke.
- Commit and push `feat/m7c-ntp`.
