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

assert peer.tftp_data(1)[:4] == b"\x00\x03\x00\x01"
assert len(peer.tftp_data(1)) == 516
assert peer.tftp_data(2)[:4] == b"\x00\x03\x00\x02"
assert len(peer.tftp_data(2)) == 192
assert peer.tftp_data(1)[4] == 0
assert peer.tftp_data(2)[4] == 0
print("PASS: M7D TFTP peer behavior")
PY
