# M7E File Transfer Implementation Plan

## Task 1: Bounded FatFs file handles

- Add a four-slot generation-checked file table.
- Add dedicated open/read/write/sync/close syscalls and user wrappers.
- Add host contracts and a small QEMU persistence probe.

## Task 2: Streaming SHA-256

- Add a freestanding incremental SHA-256 implementation.
- Test empty, `abc`, split-update, and 1 MiB deterministic input vectors.

## Task 3: 1 MiB TFTP transfer

- Extend the client and peer to 2048 full blocks plus final empty block.
- Write every block before ACK, stream SHA-256, reopen, reread, and verify.
- Add duplicate block and timeout/retry behavior.

## Task 4: Acceptance

- Run focused tests, full host regression, static checks, kernel build, and
  real QEMU/TAP acceptance.
- Commit and push `feat/m7e-file-transfer`.
