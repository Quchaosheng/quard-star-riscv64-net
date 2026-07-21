#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
grep -Fq 'm7d_tftp_get' "$root/user/Makefile"
grep -Fq 'QS_M7D_TEST' "$root/scripts/m6b-build.sh"
grep -Fq 'QS:M7D_TFTP_RRQ_OK' "$root/user/m7d_tftp_get.c"
grep -Fq 'QS:M7D_TFTP_DATA%d_OK' "$root/user/m7d_tftp_get.c"
grep -Fq 'QS:M7D_TFTP_CHECKSUM_OK' "$root/user/m7d_tftp_get.c"
grep -Fq -- '--require-tftp' "$root/scripts/m5-peer.py"

echo 'PASS: M7D TFTP build and guest contracts'
