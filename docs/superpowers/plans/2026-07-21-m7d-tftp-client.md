# M7D TFTP Read Client Implementation Plan

## Task 1: TFTP codec

- Add host-tested RRQ, DATA validation, ACK encoding, and streaming checksum.
- Cover malformed opcode, filename/mode, block order, length, and server TID.

## Task 2: Guest transfer

- Add `m7d_tftp_get` after M7C using the existing UDP socket ABI.
- Receive a deterministic 700-byte object as 512-byte and 188-byte blocks.
- Validate total bytes and checksum without storing the complete file.

## Task 3: TAP peer and acceptance

- Add strict TFTP peer state and statistics.
- Add build/smoke contracts, full host regression, and real QEMU/TAP evidence.
- Commit and push `feat/m7d-tftp`.
