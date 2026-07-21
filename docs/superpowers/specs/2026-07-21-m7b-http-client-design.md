# M7B HTTP Client Design

## Goal

Add the smallest first-party HTTP/1.0 GET client on top of the existing DNS,
TCP, and socket paths. The client proves that an application can resolve a
name, establish a TCP connection, exchange bounded application data, validate
a response, and close the connection through the existing ABI.

## Scope

- Resolve `m7a.test` through the existing M7A DNS syscall.
- Connect to `192.168.100.1:8080` with the existing TCP socket syscall.
- Send one fixed HTTP/1.0 `GET /m7b.txt` request with `Connection: close`.
- Receive and validate an HTTP `200` response with a fixed `Content-Length` and
  body, allowing the response to arrive in multiple TCP reads.
- Close the socket and emit deterministic guest markers.
- Add a local TAP HTTP peer mode; acceptance must not use the public network.

Out of scope: HTTPS/TLS, chunked transfer, redirects, cookies, caching,
request headers beyond the fixed request, persistent connections, HTTP/2, and
file-system writes.

## Architecture

The user program owns only the bounded request/response buffer and HTTP
validation. DNS resolution, TCP state, retransmission, and socket waiting stay
in the existing kernel paths. The peer reuses the existing deterministic TCP
server machinery with a separate port and payload contract, so M6C2 behavior is
not weakened or silently reused for HTTP evidence.

The response parser requires the status line `HTTP/1.0 200 OK`, a decimal
`Content-Length` equal to the fixed body size, the header terminator, and an
exact body match. It rejects malformed status, missing or duplicate length,
oversized content, premature close, and unexpected body bytes.

## Evidence

The guest emits, exactly once and in order:

```text
QS:M7B_HTTP_DNS_OK
QS:M7B_HTTP_CONNECT_OK
QS:M7B_HTTP_RESPONSE_OK
QS:M7B_HTTP_CLOSE_OK
QS:TEST_PASS:m7b-smoke
```

Host tests cover the response parser and source contracts. The TAP peer test
covers the request method, path, host header, TCP tuple, response bytes, and
malformed request rejection. Real QEMU/TAP smoke requires all cumulative
M0-M7A markers plus the M7B markers and HTTP peer statistics.

## Completion Criteria

M7B is complete only when the host suite, static checks, kernel build, and real
QEMU/TAP HTTP acceptance pass. A guest-only marker without a validated peer
request and response does not count as completion.
