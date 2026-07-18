#!/usr/bin/env bash
set -Eeu

iface=${1:-${QS_TAP_IFACE:-tap0}}
address=${QS_TAP_ADDR:-192.168.100.1/24}
tap_user=${QS_TAP_USER:-$(id -un)}
operation=initialization

on_error()
{
    status=$?
    echo "tap-up: failed: $operation" >&2
    exit "$status"
}
trap on_error ERR

run_mutation()
{
    if [ "$(id -u)" -eq 0 ] || ip "$@" 2>/dev/null; then
        if [ "$(id -u)" -eq 0 ]; then
            ip "$@"
        fi
        return
    fi
    "${QS_SUDO:-sudo}" ip "$@"
}

link_output=
operation="ip link show dev $iface"
if link_output=$(ip link show dev "$iface" 2>&1); then
    link_exists=1
else
    case "$link_output" in
        *'does not exist'*|*'Cannot find device'*) link_exists=0 ;;
        *) echo "$link_output" >&2; false ;;
    esac
fi

if [ "$link_exists" -eq 0 ]; then
    operation="ip tuntap add dev $iface mode tap user $tap_user"
    run_mutation tuntap add dev "$iface" mode tap user "$tap_user"
    if [ -n "${QS_TAP_CREATED_FILE:-}" ]; then
        : >"$QS_TAP_CREATED_FILE"
    fi
fi

operation="ip addr show dev $iface"
address_output=$(ip addr show dev "$iface")
if ! printf '%s\n' "$address_output" | grep -Fq "inet $address"; then
    operation="ip addr add $address dev $iface"
    run_mutation addr add "$address" dev "$iface"
fi

operation="ip link set dev $iface up"
run_mutation link set dev "$iface" up
