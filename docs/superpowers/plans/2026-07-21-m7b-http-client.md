# M7B HTTP Client Implementation Plan

## Task 1: HTTP response codec

- Add a small host-testable parser for the fixed HTTP response contract.
- Cover valid split input, malformed status, missing/duplicate length,
  oversized body, truncation, and extra bytes.
- Keep parsing bounded and do not write a body result before validation.

## Task 2: Guest HTTP probe

- Add `user/m7b_http_get.c` after the M7A application chain; use the existing
  validated TCP peer port `4800` for the first HTTP slice.
- Resolve `m7a.test`, connect to port 8080, send the fixed request, receive
  until the complete response is validated, and close on every failure path.
- Add M7B build flags, markers, and cumulative smoke requirements.

## Task 3: Deterministic TAP HTTP peer

- Extend `m5-peer.py` with `--require-http`, port 8080 handling, request and
  response counters, and strict request validation.
- Add focused peer tests for valid and malformed requests.

## Task 4: Verification

- Register host tests and targets.
- Run all host tests, shell/Python syntax checks, kernel build, and real
  QEMU/TAP smoke.
- Commit and push `feat/m7b-http`.
