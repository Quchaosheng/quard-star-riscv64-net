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

query = bytearray(48)
query[0] = 0x1b
query[40:44] = bytes.fromhex("83aa7ee4")
reply = peer.ntp_response(bytes(query))
assert reply is not None and len(reply) == 48
assert reply[0] == 0x24 and reply[1] == 1
assert reply[24:32] == query[40:48]
assert reply[40:44] == bytes.fromhex("83aa7efb")
assert peer.ntp_response(bytes(query[:47])) is None
query[0] = 0x23
assert peer.ntp_response(bytes(query)) is None
print("PASS: M7C NTP peer behavior")
PY
