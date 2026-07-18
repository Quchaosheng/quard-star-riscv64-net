#!/usr/bin/env bash
set -Eeu

iface=${1:-${QS_TAP_IFACE:-tap0}}
operation="ip link show dev $iface"

on_error()
{
    status=$?
    echo "tap-down: failed: $operation" >&2
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
if link_output=$(ip link show dev "$iface" 2>&1); then
    link_exists=1
else
    case "$link_output" in
        *'does not exist'*|*'Cannot find device'*) link_exists=0 ;;
        *) echo "$link_output" >&2; false ;;
    esac
fi

if [ "$link_exists" -eq 1 ]; then
    operation="ip link delete dev $iface"
    run_mutation link delete dev "$iface"
fi
