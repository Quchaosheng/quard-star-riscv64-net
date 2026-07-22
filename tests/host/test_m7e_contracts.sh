#!/usr/bin/env bash
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
grep -Fq 'm7e_tftp_get' "$root/user/Makefile"
grep -Fq '__NR_file_open' "$root/kernel/include/timeros/syscall.h"
grep -Fq 'sys_file_write' "$root/user/m7e_tftp_get.c"
grep -Fq 'sys_file_read' "$root/user/m7e_tftp_get.c"
grep -Fq 'QS:M7E_TFTP_REOPEN_OK' "$root/user/m7e_tftp_get.c"
grep -Fq 'QS:TEST_PASS:m7e-smoke' "$root/kernel/src/selftest.c"
echo 'PASS: M7E file transfer contracts'
