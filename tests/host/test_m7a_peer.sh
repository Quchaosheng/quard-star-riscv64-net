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

query = b"\x12\x34\x01\x00\x00\x01\x00\x00\x00\x00\x00\x00" \
    b"\x03m7a\x04test\x00\x00\x01\x00\x01"
reply = peer.dns_response(query, bytes((192, 168, 100, 1)))
assert reply is not None and len(reply) > len(query)
assert reply[:2] == query[:2]
assert reply[12:12 + len(query[12:])] == query[12:]

timeout = b"\x12\x34\x01\x00\x00\x01\x00\x00\x00\x00\x00\x00" \
    b"\x07timeout\x03m7a\x00\x00\x01\x00\x01"
assert peer.dns_response(timeout, bytes((192, 168, 100, 1))) == b""

wrong_name = query[:-1] + b"\x02"
assert peer.dns_response(wrong_name, bytes((192, 168, 100, 1))) is None
wrong_type = query[:-3] + b"\x00\x1c\x00\x01"
assert peer.dns_response(wrong_type, bytes((192, 168, 100, 1))) is None
print("PASS: M7A DNS peer query behavior")
PY
