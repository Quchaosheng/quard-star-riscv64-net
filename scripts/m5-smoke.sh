#!/usr/bin/env bash
set -eu

script_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
root=${QS_ROOT:-$script_root}
stage=${QS_STAGE:-m5}
test_name=${QS_TEST_NAME:-m5-smoke}
iface=${QS_TAP_IFACE:-tap0}
raw_count=${QS_M5_RAW_COUNT:-32}
peer=${QS_M5_PEER:-$script_root/scripts/m5-peer.py}
out=$root/out/$stage
ready=$out/m5-peer.ready
stats=$out/m5-peer.stats
created=$out/m5-tap.created
tap_created=0
peer_pid=
smoke_pid=

if [ "$test_name" != m5-smoke ]; then
  echo "error: unsupported M5 test name $test_name" >&2
  exit 1
fi

cleanup()
{
  status=$?
  for child in "$smoke_pid" "$peer_pid"; do
    if [ -n "$child" ] && kill -0 "$child" 2>/dev/null; then
      kill "$child" 2>/dev/null || true
      wait "$child" 2>/dev/null || true
    fi
  done
  if [ "$tap_created" -eq 1 ]; then
    "$script_root/scripts/tap-down.sh" "$iface" >/dev/null 2>&1 || true
  fi
  exit "$status"
}
trap cleanup EXIT INT TERM

mkdir -p "$out"
rm -f "$ready" "$stats" "$created"
: >"$ready"
: >"$stats"
if [ "${QS_TAP_MANAGED:-1}" != 0 ]; then
  QS_TAP_CREATED_FILE=$created "$script_root/scripts/tap-up.sh" "$iface"
  [ ! -f "$created" ] || tap_created=1
fi

set -- "$peer" "$iface" --raw-count "$raw_count" \
  --timeout "${QS_PEER_TIMEOUT:-${QS_SMOKE_TIMEOUT:-60}}" \
  --ready-file "$ready" --stats-file "$stats"
peer_needs_sudo=0
if [ "${QS_FORCE_PEER_SUDO:-0}" = 1 ]; then
  peer_needs_sudo=1
elif [ "$(id -u)" -ne 0 ] && ! python3 -c \
  'import socket; socket.socket(socket.AF_PACKET, socket.SOCK_RAW, 0).close()' \
  >/dev/null 2>&1; then
  peer_needs_sudo=1
fi
if [ "$peer_needs_sudo" -eq 1 ]; then
  sudo_cmd=${QS_SUDO:-sudo}
  if ! command -v "$sudo_cmd" >/dev/null 2>&1; then
    echo 'error: M5 peer requires CAP_NET_RAW or an available sudo command' >&2
    exit 1
  fi
  "$sudo_cmd" "$@" &
else
  "$@" &
fi
peer_pid=$!

i=0
while [ ! -s "$ready" ]; do
  if ! kill -0 "$peer_pid" 2>/dev/null; then
    wait "$peer_pid" || true
    echo 'error: M5 peer exited before ready; CAP_NET_RAW or sudo may be required' >&2
    exit 1
  fi
  if [ "$i" -ge 100 ]; then
    echo 'error: timed out waiting for M5 peer readiness' >&2
    exit 1
  fi
  sleep 0.05
  i=$((i + 1))
done

export QS_ROOT=$root
export QS_STAGE=$stage
export QS_TEST_NAME=$test_name
export QS_TAP_IFACE=$iface
export QS_EXTRA_MARKERS="QS:VIRTQUEUE_OK QS:BLOCK_IRQ_OK QS:BLOCK_STRESS_OK QS:FATFS_OK QS:NET_LINK_OK QS:NET_IRQ_OK QS:NET_TX_OK QS:NET_RX_OK QS:NET_RESET_OK QS:NET_RESETS:1 QS:NET_STRESS_FRAMES:$raw_count QS:M5_ARP_OK QS:M5_PING_OK QS:TEST_PASS:$test_name"
export QS_SMOKE_TIMEOUT=${QS_SMOKE_TIMEOUT:-60}

"$script_root/scripts/m2c-smoke.sh" &
smoke_pid=$!
set +e
wait "$smoke_pid"
smoke_status=$?
smoke_pid=
if [ "$smoke_status" -ne 0 ] && kill -0 "$peer_pid" 2>/dev/null; then
  kill "$peer_pid" 2>/dev/null || true
fi
wait "$peer_pid"
peer_status=$?
peer_pid=
set -e

if [ "$smoke_status" -ne 0 ]; then
  echo "error: M5 guest acceptance exit status $smoke_status" >&2
  exit 1
fi
if [ "$peer_status" -ne 0 ]; then
  echo "error: M5 peer exit status $peer_status" >&2
  exit 1
fi
if ! python3 - "$stats" "$raw_count" <<'PY'
import json
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
expected_raw = int(sys.argv[2])
try:
    data = json.loads(path.read_text(encoding="utf-8"))
except (OSError, ValueError):
    raise SystemExit(1)
required = (
    data.get("raw_frames") == expected_raw and
    data.get("arp_requests", 0) >= 1 and
    data.get("arp_replies", 0) >= 1 and
    data.get("guest_echo_requests", 0) >= 1 and
    data.get("guest_echo_replies", 0) >= 1 and
    data.get("host_echo_requests", 0) >= 1 and
    data.get("host_echo_replies", 0) >= 1
)
raise SystemExit(0 if required else 1)
PY
then
  echo 'error: M5 peer did not observe complete ARP and ICMP exchange' >&2
  exit 1
fi

echo "PASS: $test_name TAP ARP/ICMP acceptance"
