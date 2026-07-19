#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)

fail()
{
  echo "FAIL: $*" >&2
  exit 1
}

require()
{
  pattern=$1
  file=$2
  grep -Eq -- "$pattern" "$root/$file" || fail "$file missing $pattern"
}

require '^m6a-build:' Makefile
require '^m6a-smoke:' Makefile
require 'QS_STAGE=m6a' scripts/m6a-build.sh
require 'QS_M6A_TEST' scripts/m6a-build.sh
for flag in QS_M2C_TEST QS_M3_TEST QS_M4_TEST QS_M5_TEST; do
  require "$flag" scripts/m6a-build.sh
done
require 'QS_NET_ITERATIONS=32' scripts/m6a-build.sh
require 'QS_NET_RESETS=1' scripts/m6a-build.sh
require 'm5-smoke\.sh' scripts/m6a-smoke.sh

stack=$root/kernel/src/net/net_stack.c
init=$(sed -n '/net_err_t net_stack_init(void)/,/^}/p' "$stack")
worker=$(sed -n '/void net_stack_worker(void \*arg)/,/^}/p' "$stack")
line_of() { printf '%s\n' "$init" | grep -En -- "$1" | head -n 1 | cut -d: -f1; }
net_sys_line=$(line_of 'net_sys_init')
pktbuf_line=$(line_of 'pktbuf_init')
timer_line=$(line_of 'net_timer_init')
netif_line=$(line_of 'netif_init')
loop_line=$(line_of 'loop_init')
open_line=$(line_of 'netif_open\("eth0"')
[ "$net_sys_line" -lt "$pktbuf_line" ] || fail 'net_sys_init must precede pktbuf_init'
[ "$pktbuf_line" -lt "$timer_line" ] || fail 'net_timer_init must follow pktbuf_init'
[ "$timer_line" -lt "$netif_line" ] || fail 'net_timer_init must precede netif_init'
[ "$loop_line" -lt "$open_line" ] || fail 'loop_init must precede opening eth0'
printf '%s\n' "$worker" | grep -q 'sys_time_curr' || fail 'worker must establish timer baseline'
printf '%s\n' "$worker" | grep -q 'sys_time_goes' || fail 'worker must advance time'
printf '%s\n' "$worker" | grep -q 'net_timer_check_tmo' || fail 'worker must run shared timers'
printf '%s\n' "$worker" | grep -q 'net_timer_first_tmo' || fail 'worker poll must be timer bounded'
printf '%s\n' "$worker" | grep -q 'net_stack_process_input' || fail 'worker must drain loop input'

require 'fixq_init' kernel/src/net/net_stack.c
require 'fixq_send' kernel/src/net/net_stack.c
require 'NET_ERR_FULL' kernel/src/net/net_stack.c
require 'NET_ERR_TMO' kernel/src/net/net_stack.c
require 'fixq_recv' kernel/src/net/net_stack.c
require 'sys_sem_wait' kernel/src/net/fixq.c
require 'loop_get_netif' kernel/src/net/net_stack.c
require 'icmpv4_out_echo' kernel/src/net/net_stack.c
require '0x6[dD]36' kernel/src/net/net_stack.c
require 'QS:M6_QUEUE_OK' kernel/src/net/net_stack.c
require 'QS:M6_LOOP_OK' kernel/src/net/net_stack.c
require 'QS:M6_ARP_TIMER_OK' kernel/src/net/arp.c
arp_callback=$(sed -n '/static void arp_cache_tmo/,/^}/p' "$root/kernel/src/net/arp.c")
printf '%s\n' "$arp_callback" | grep -q 'QS:M6_ARP_TIMER_OK' || \
  fail 'ARP marker must be emitted by arp_cache_tmo'
require 'm6_mark_queue' kernel/include/timeros/selftest.h
require 'm6_mark_arp_timer' kernel/include/timeros/selftest.h
require 'm6_mark_loop' kernel/include/timeros/selftest.h
require 'M6A_ALL_DONE' kernel/src/selftest.c
require 'QS:TEST_PASS:m6a-smoke' kernel/src/selftest.c

echo 'PASS: M6A source and build contracts'
