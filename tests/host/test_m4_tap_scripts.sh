#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp/bin" "$tmp/state"

cat >"$tmp/bin/id" <<'SH'
#!/usr/bin/env bash
if [ "${1:-}" = -u ]; then
  echo "${FAKE_EUID:-1000}"
  exit 0
fi
exit 1
SH

cat >"$tmp/bin/sudo" <<'SH'
#!/usr/bin/env bash
printf 'sudo %s\n' "$*" >>"$QS_FAKE_LOG"
exec "$@"
SH

cat >"$tmp/bin/ip" <<'SH'
#!/usr/bin/env bash
set -eu
printf 'ip %s\n' "$*" >>"$QS_FAKE_LOG"
if [ "${FAKE_IP_FAIL:-}" = addr-add ]; then
  case "$*" in
    addr\ add*) exit 1 ;;
  esac
fi
iface=${QS_TAP_IFACE:-tap0}
case "$*" in
  "link show dev $iface")
    if [ "${FAKE_IP_QUERY_FAIL:-0}" = 1 ]; then
      echo 'permission denied' >&2
      exit 2
    fi
    if ! test -f "$QS_FAKE_STATE/link"; then
      echo "Device $iface does not exist" >&2
      exit 1
    fi
    ;;
  "addr show dev $iface")
    test -f "$QS_FAKE_STATE/link" || exit 1
    if test -f "$QS_FAKE_STATE/addr"; then
      printf 'inet %s\n' "$(cat "$QS_FAKE_STATE/addr")"
    fi
    ;;
  "tuntap add dev $iface mode tap user "*) touch "$QS_FAKE_STATE/link" ;;
  "addr add "*" dev $iface")
    printf '%s\n' "$3" >"$QS_FAKE_STATE/addr"
    ;;
  "link set dev $iface up") touch "$QS_FAKE_STATE/up" ;;
  "link delete dev $iface") rm -f "$QS_FAKE_STATE/link" "$QS_FAKE_STATE/addr" "$QS_FAKE_STATE/up" ;;
  *) exit 1 ;;
esac
SH
chmod +x "$tmp/bin/id" "$tmp/bin/sudo" "$tmp/bin/ip"

export PATH="$tmp/bin:$PATH"
export QS_FAKE_LOG="$tmp/ip.log"
export QS_FAKE_STATE="$tmp/state"
export QS_TAP_IFACE=tap-test
export QS_TAP_USER=alice
export QS_TAP_ADDR=192.168.100.1/24

"$root/scripts/tap-up.sh" tap-test
test -f "$tmp/state/link"
test -f "$tmp/state/up"
test "$(cat "$tmp/state/addr")" = 192.168.100.1/24
grep -Fq 'tuntap add dev tap-test mode tap user alice' "$tmp/ip.log"
grep -Fq 'addr add 192.168.100.1/24 dev tap-test' "$tmp/ip.log"
grep -Fq 'link set dev tap-test up' "$tmp/ip.log"

adds=$(grep -Fc 'tuntap add dev tap-test' "$tmp/ip.log")
addresses=$(grep -Fc 'addr add 192.168.100.1/24' "$tmp/ip.log")
"$root/scripts/tap-up.sh" tap-test
test "$(grep -Fc 'tuntap add dev tap-test' "$tmp/ip.log")" -eq "$adds"
test "$(grep -Fc 'addr add 192.168.100.1/24' "$tmp/ip.log")" -eq "$addresses"

"$root/scripts/tap-down.sh" tap-test

export FAKE_IP_QUERY_FAIL=1
if "$root/scripts/tap-down.sh" tap-test >"$tmp/query.out" 2>&1; then
  echo 'FAIL: TAP query failure was mistaken for an absent interface' >&2
  exit 1
fi
grep -Fq 'link show' "$tmp/query.out"
unset FAKE_IP_QUERY_FAIL
test ! -f "$tmp/state/link"
"$root/scripts/tap-down.sh" tap-test

export FAKE_IP_FAIL=addr-add
if "$root/scripts/tap-up.sh" tap-test >"$tmp/failure.out" 2>&1; then
  echo 'FAIL: privileged TAP command failure was ignored' >&2
  exit 1
fi
grep -Fq 'addr add' "$tmp/failure.out"
unset FAKE_IP_FAIL

PYTHONDONTWRITEBYTECODE=1 python3 - "$root" <<'PY'
import importlib.util
import pathlib
import struct
import sys

root = pathlib.Path(sys.argv[1])
spec = importlib.util.spec_from_file_location("m4_peer", root / "scripts/m4-peer.py")
peer = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(peer)

sequence = 7
payload = bytes((sequence ^ offset ^ 0x5A) & 0xFF for offset in range(32))
body = struct.pack("!IH", sequence, len(payload)) + payload
body += struct.pack("!I", peer.payload_checksum(payload))
request = peer.BROADCAST + peer.GUEST_MAC + struct.pack("!H", peer.ETHERTYPE) + body
request = request.ljust(60, b"\0")
assert peer.decode_request(request) == (sequence, payload)

response = peer.encode_response(sequence, payload)
assert len(response) == 60
assert response[:6] == peer.GUEST_MAC
assert response[6:12] == peer.HOST_MAC

bad_frames = [
    request[:20],
    b"\0" * 6 + request[6:],
    request[:12] + struct.pack("!H", 0x0800) + request[14:],
    request[:18] + struct.pack("!H", 1000) + request[20:],
    request[:52] + b"\0\0\0\0" + request[56:],
]
bad_payload = bytearray(request)
bad_payload[20] ^= 1
bad_payload[52:56] = struct.pack("!I", peer.payload_checksum(bad_payload[20:52]))
bad_frames.append(bytes(bad_payload))
for frame in bad_frames:
    try:
        peer.decode_request(frame)
    except ValueError:
        pass
    else:
        raise AssertionError("malformed M4 frame accepted")
PY

echo 'PASS: M4 TAP scripts and raw peer helpers'
