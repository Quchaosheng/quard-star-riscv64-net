# M7E File Transfer Design

## Goal

Complete the file-transfer acceptance path by adding a bounded FatFs user API
and extending the TFTP client to download, persist, reopen, and SHA-256 verify a
deterministic 1 MiB object.

## File API

Add dedicated file syscalls rather than overloading console and socket file
descriptors. A fixed kernel table owns four `FIL` objects and generation-based
handles. The first slice supports create/truncate write, read, sync, and close.
Paths must be relative leaf names with a bounded length; separators, drive
prefixes, and traversal are rejected. All FatFs calls are serialized because
the configured library is not reentrant.

Closing a writable file always syncs before release. Invalid handles, bad user
pointers, table exhaustion, short writes, and FatFs errors return bounded
errors without panic.

## TFTP Transfer

The local peer serves exactly 1 MiB as 2048 full DATA blocks followed by block
2049 with zero payload, as required when the file size is an exact multiple of
512 bytes. The client locks the server TID, accepts blocks in order, writes
each payload to `0:/m7e.bin`, updates SHA-256 incrementally, and ACKs only after
the corresponding write succeeds.

After the final ACK, the client syncs and closes the file, reopens it for read,
recomputes SHA-256, and compares both digests with the fixed expected value.
The complete file is never buffered in memory.

## Evidence

- Host tests cover the file handle table contract and SHA-256 known vectors.
- TFTP tests cover first, middle, final full, and final empty blocks, wrong TID,
  wrong block, duplicate block, and bounded timeout/retry.
- Real QEMU/TAP smoke requires 2049 DATA packets, 2049 ACKs, zero outstanding
  transfer state, 1 MiB written, reopen/read success, and exact SHA-256.
- All M0-M7D tests remain regression requirements.

## Out Of Scope

Directories, rename/unlink, append, shared descriptors across fork, TFTP
upload, option negotiation, concurrent transfers, and arbitrary public paths.
