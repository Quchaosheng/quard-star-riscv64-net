# M7C NTP Client Design

## Goal

Add the smallest first-party NTP client over the existing UDP/socket path. It
will query a deterministic local NTP peer, validate one server response, and
convert the NTP transmit timestamp to Unix seconds without changing the kernel
wall clock.

## Scope

- Send one 48-byte NTP client request to `192.168.100.1:123`.
- Validate response length, leap/version/mode fields, stratum, originate
  timestamp, and nonzero transmit timestamp.
- Convert the 64-bit NTP timestamp from the 1900 epoch to Unix seconds.
- Exercise one bounded timeout against an unused local address.
- Add deterministic TAP peer statistics and guest markers.

Out of scope: clock discipline, slew/step adjustment, NTP extensions,
authentication, multiple servers, polling, IPv6, and public network access.

## Evidence

The guest emits exactly once and in order:

```text
QS:M7C_NTP_QUERY_OK
QS:M7C_NTP_RESPONSE_OK
QS:M7C_NTP_TIMEOUT_OK
QS:TEST_PASS:m7c-smoke
```

Host tests cover request fields, timestamp conversion, invalid mode/stratum,
wrong originate timestamp, truncation, and timeout contracts. Real QEMU/TAP
acceptance requires cumulative M0-M7B markers and validated NTP peer counters.
