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

"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror \
  "$root/scripts/m4-peer.c" -o "$tmp/m4-peer"
"$tmp/m4-peer" --self-test

echo 'PASS: M4 TAP scripts and raw peer helpers'
