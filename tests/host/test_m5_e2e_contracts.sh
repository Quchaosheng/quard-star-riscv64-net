#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
status=0

require_text() {
  file=$1
  text=$2
  message=$3
  if ! grep -Fq -- "$text" "$root/$file" 2>/dev/null; then
    echo "FAIL: $message" >&2
    status=1
  fi
}

require_text Makefile 'm5-build m5-smoke' \
  'the root build must expose M5 build and smoke targets'
require_text scripts/m5-build.sh '-DQS_M5_TEST' \
  'M5 build must compile the guest network probe'
require_text scripts/m5-smoke.sh 'm5-peer.py' \
  'M5 smoke must use the ARP/ICMP TAP peer'
require_text scripts/m5-smoke.sh 'm2c-smoke.sh' \
  'M5 smoke must retain the existing guest marker runner'
require_text scripts/m5-peer.py 'encode_arp_reply' \
  'M5 peer must answer guest ARP requests'
require_text scripts/m5-peer.py 'encode_icmp_echo' \
  'M5 peer must exchange ICMP echo frames'
require_text kernel/src/selftest.c 'M5_NET_ARP_DONE' \
  'M5 selftest must require ARP completion'
require_text kernel/src/selftest.c 'm5_mark_net_ping' \
  'M5 selftest must require an ICMP reply'
require_text kernel/src/net/net_stack.c 'icmpv4_out_echo' \
  'the guest probe must send an ICMP echo'
require_text kernel/src/net/net_stack.c 'm5_mark_net_arp' \
  'the guest probe must report ARP completion'
require_text kernel/include/timeros/net/icmpv4.h 'icmpv4_get_stats' \
  'the stack needs a small ICMP reply observation boundary'

if [ "$status" -ne 0 ]; then
  exit "$status"
fi
echo 'PASS: M5 end-to-end source contracts'
