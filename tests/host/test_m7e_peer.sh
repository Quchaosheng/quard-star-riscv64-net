#!/usr/bin/env bash
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
python3 - "$root/scripts/m5-peer.py" <<'PY'
import importlib.util
import sys
spec = importlib.util.spec_from_file_location("peer", sys.argv[1])
peer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(peer)
assert len(peer.tftp_data(1, True)) == 516
assert len(peer.tftp_data(2048, True)) == 516
assert peer.tftp_data(2049, True) == b"\x00\x03\x08\x01"
assert peer.tftp_oack() == b"\x00\x06windowsize\x004\x00"
assert peer.tftp_data(1, True)[4:8] == bytes((0, 1, 2, 3))
assert peer.tftp_data(2, True)[4:8] == bytes((0, 1, 2, 3))
print("PASS: M7E 1 MiB TFTP peer data")
PY
