# M7D TFTP Read Client Design

## Goal

Add a bounded TFTP read client over the existing UDP/socket path. The first
slice downloads a deterministic multi-block object, validates block order and
server transfer ID, acknowledges every block, and checks the streamed content
without adding a second UDP implementation.

## Scope

- Encode one RRQ for `m7d.bin` in `octet` mode to `192.168.100.1:69`.
- Accept DATA from one deterministic server transfer port and lock that TID.
- Receive block 1 with 512 bytes and block 2 with a final short payload.
- ACK each accepted block and reject wrong opcode, block, port, or oversized
  packet.
- Compute a streaming checksum and verify the exact total length.
- Add one bounded timeout/error case and deterministic TAP evidence.

Out of scope: file-system writes, WRQ/upload, options, block-size negotiation,
rollover, concurrent transfers, and the final 1 MiB SHA-256 acceptance target.

## Source Boundary

The documented source repository `Quchaosheng/tiny-tcpip-stack` currently
returns `Repository not found`, and no local copy is available. This slice is
therefore an incremental first-party implementation against the existing
socket ABI, not a claimed migration of unavailable TFTP source.

## Evidence

Guest markers cover RRQ, both DATA blocks, checksum, timeout, and completion.
The TAP peer records RRQ, DATA, ACK, retransmission/error, and outstanding
transfer state. Full M0-M7C regression remains mandatory.
