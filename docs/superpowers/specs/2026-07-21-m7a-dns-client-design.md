# M7A DNS Client Design

## Goal

Add the smallest first-party DNS client to the existing UDP and socket stack.
The client resolves one hostname to an IPv4 address through a configured DNS
server and exposes deterministic host and QEMU/TAP evidence. It does not add
TFTP, HTTP, NTP, caching, or a second UDP implementation.

## Scope

- Encode one standard DNS query for type `A`, class `IN`.
- Decode one response with a matching transaction ID and question name.
- Follow compressed names in answer records with bounded pointer validation.
- Return the first valid IPv4 answer and reject malformed, truncated, failed,
  mismatched, or unsupported responses.
- Send and receive through the existing UDP socket/syscall path.
- Use a local fake DNS peer at `192.168.100.1`; acceptance must not depend on
  public DNS or Internet routing.

Out of scope: CNAME chasing, IPv6, EDNS, DNSSEC, recursive service behavior,
negative caching, retries beyond one bounded timeout/retry policy, and a public
user-facing resolver library.

## Architecture

The DNS codec lives under `kernel/src/net` with a small public header. It owns
only wire-format parsing and validation; UDP sockets remain responsible for
transport, timeout, and buffer ownership. A DNS request is submitted through
the existing network executor, and the blocking receive uses the existing UDP
socket wait path. No new protocol worker or RPC queue is introduced.

The M7A guest probe uses a fixed resolver address and hostname, sends one query,
checks the returned address, and emits exact markers for query, resolution, and
timeout/error behavior. The TAP peer validates the query tuple, transaction ID,
question, and response checksum before returning a deterministic answer.

## Wire Validation

The encoder must reject empty or overlong labels, invalid hostname syntax, and
names whose encoded form exceeds 255 bytes. The decoder must validate the DNS
header length, response bit, reply code, question count, answer bounds, label
lengths, compression pointer range, record class, record type, and `RDLENGTH`.
Pointers may not form an unbounded loop; parsing is bounded by the packet size
and a fixed jump count.

Only an `A` record with class `IN` and `RDLENGTH == 4` is accepted. A response
with no usable answer returns a specific negative network error. The caller
must not receive a partially parsed address.

## Evidence And Tests

- Host tests cover label encoding, transaction/question matching, compression,
  truncation, pointer loops, wrong IDs, error replies, unsupported records,
  and exact IPv4 extraction under sanitizers.
- A fake peer test proves the UDP query and response path without QEMU.
- QEMU/TAP acceptance starts the local DNS peer, requires the exact M7A
  markers once and in order, and checks nonzero query/response counters.
- Existing M0-M6C2 tests and smoke paths remain unchanged and must pass.

## Completion Criteria

M7A is complete only when the host suite, static checks, and real QEMU/TAP DNS
acceptance pass. The output must identify the resolved IPv4 address and the
peer must report a validated query and response; fabricated guest-only markers
are not sufficient.
