#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
PYTHONDONTWRITEBYTECODE=1 python3 - "$root" <<'PY'
import importlib.util
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
spec = importlib.util.spec_from_file_location("m5_peer", root / "scripts/m5-peer.py")
assert spec and spec.loader
peer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(peer)

assert peer.HTTP_PORT == 4800
assert peer.HTTP_REQUEST.startswith(b"GET /m7b.txt HTTP/1.0\r\n")
assert b"Host: m7a.test\r\n" in peer.HTTP_REQUEST
assert peer.HTTP_RESPONSE.endswith(b"m7b-http-body")
assert b"Content-Length: 13\r\n" in peer.HTTP_RESPONSE

bad_method = peer.HTTP_REQUEST.replace(b"GET", b"POST", 1)
assert bad_method != peer.HTTP_REQUEST
assert not bad_method.startswith(b"GET /m7b.txt HTTP/1.0")
print("PASS: M7B HTTP peer contract")
PY
