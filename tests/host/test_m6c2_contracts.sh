#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
fail() { echo "FAIL: $*" >&2; exit 1; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

grep -q '^m6c2-build:' "$root/Makefile" || fail 'missing m6c2-build target'
grep -q '^m6c2-smoke:' "$root/Makefile" || fail 'missing m6c2-smoke target'
grep -qx 'export QS_M6C2_TEST=1' "$root/scripts/m6c2-build.sh" || \
  fail 'missing M6C2 build flag'
grep -q 'm6c1-build.sh' "$root/scripts/m6c2-build.sh" || \
  fail 'M6C2 build must reuse M6C1'
grep -qx 'export QS_STAGE=m6c2' "$root/scripts/m6c2-smoke.sh" || \
  fail 'missing M6C2 smoke stage'
grep -qx 'export QS_TEST_NAME=m6c2-smoke' "$root/scripts/m6c2-smoke.sh" || \
  fail 'missing M6C2 smoke name'
grep -q 'm6c1-smoke.sh' "$root/scripts/m6c2-smoke.sh" || \
  fail 'M6C2 smoke must reuse M6C1'
for definition in '__NR_listen 201' '__NR_accept 202'; do
  grep -qx "#define $definition" "$root/kernel/include/timeros/syscall.h" || \
    fail "missing exact $definition"
done
for name in listen accept; do
  grep -q "case __NR_$name:" "$root/kernel/src/syscall.c" || \
    fail "missing $name dispatch"
  grep -q "sys_$name" "$root/kernel/lib/app.c" || \
    fail "missing user $name wrapper"
done
grep -q 'TCP_STATE_LISTEN' "$root/kernel/include/timeros/net/tcp.h" || \
  fail 'missing LISTEN state'
grep -q 'TCP_STATE_SYN_RECEIVED' "$root/kernel/include/timeros/net/tcp.h" || \
  fail 'missing SYN_RECEIVED state'
grep -q 'tcp_server_echo' "$root/user/Makefile" || \
  fail 'missing server Echo target'

release_body=$(awk '
  /^static net_err_t tcp_release_now_locked\(tcp_pcb_t \*pcb\)$/ { body = 1 }
  body { print }
  body && /^}/ { exit }
' "$root/kernel/src/net/tcp.c")
slot_line=$(printf '%s\n' "$release_body" | \
  grep -n 'pcbs\[slot\] = 0;' | cut -d: -f1)
detached_line=$(printf '%s\n' "$release_body" | \
  grep -n 'detached_listeners\[slot\] = 0;' | cut -d: -f1)
child_line=$(printf '%s\n' "$release_body" | \
  grep -n 'm6c2_mark_tcp_child_close' | cut -d: -f1)
listener_line=$(printf '%s\n' "$release_body" | \
  grep -n 'm6c2_mark_tcp_listener_close' | cut -d: -f1)
[ -n "$slot_line" ] && [ -n "$detached_line" ] && \
  [ -n "$child_line" ] && \
  [ -n "$listener_line" ] && [ "$slot_line" -lt "$child_line" ] && \
  [ "$slot_line" -lt "$listener_line" ] && \
  [ "$detached_line" -lt "$child_line" ] && \
  [ "$detached_line" -lt "$listener_line" ] || \
  fail 'TCP slot release must publish before M6C2 close evidence'

function_body()
{
  awk -v signature="$2" '
    $0 == signature { body = 1 }
    body { print }
    body && /^}/ { exit }
  ' "$1"
}

for accept_function in commit release release_child; do
  accept_body=$(awk -v name="$accept_function" '
    $0 ~ "^net_err_t tcp_accept_" name "_acquired\\(" { body = 1 }
    body { print }
    body && /^}/ { exit }
  ' "$root/kernel/src/net/tcp.c")
  [ -n "$accept_body" ] || \
    fail "missing tcp_accept_${accept_function}_acquired"
  if printf '%s\n' "$accept_body" | grep -Eq \
      'tcp_(listener_cleanup_locked|listener_release_detached_locked|release_now_locked|request_release)|net_timer_(add|remove)'; then
    fail "tcp_accept_${accept_function}_acquired bypasses worker release"
  fi
done

detach_body=$(awk '
  /^net_err_t tcp_socket_detach\(tcp_pcb_t \*pcb\)$/ { body = 1 }
  body { print }
  body && /^}/ { exit }
' "$root/kernel/src/net/tcp.c")
printf '%s\n' "$detach_body" | grep -q \
  'listener_children = tcp_listener_has_children_locked(pcb)' || \
  fail 'TCP socket detach must inspect listener children'
printf '%s\n' "$detach_body" | grep -q '!listener_children' || \
  fail 'TCP socket detach must retain listener ownership while children exist'

check_close_contract()
{
  selftest=$1
  mark_body=$(function_body "$selftest" 'static void m6c2_mark(u32 bit)')
  mark_atomic_count=$(printf '%s\n' "$mark_body" | \
    grep -Ec '__atomic_fetch_or\(&completed, bit,' || true)
  [ "$mark_atomic_count" -eq 1 ] || return 1
  printf '%s\n' "$mark_body" | grep -Eq \
    '__atomic_fetch_or\(&completed, bit, __ATOMIC_(RELEASE|ACQ_REL|SEQ_CST)\);' || \
    return 1
  if printf '%s\n' "$mark_body" | grep -Eq \
      'printk|M6C2_CLOSE_DONE|m6c2_publish_close|m6c2_close_claimed'; then
    return 1
  fi

  for hook in child listener; do
    hook_body=$(function_body "$selftest" \
      "void m6c2_mark_tcp_${hook}_close(void)")
    hook_publish_count=$(printf '%s\n' "$hook_body" | \
      grep -c 'm6c2_mark(M6C2_' || true)
    [ "$hook_publish_count" -eq 1 ] || return 1
    if printf '%s\n' "$hook_body" | \
        grep -Eq 'printk|M6C2_CLOSE_DONE|m6c2_publish_close|m6c2_close_claimed'; then
      return 1
    fi
  done

  poll_body=$(function_body "$selftest" 'void m2c_selftest_poll(void)')
  poll_call_count=$(printf '%s\n' "$poll_body" | \
    grep -Fc 'm6c2_publish_close();' || true)
  [ "$poll_call_count" -eq 1 ] || return 1
  poll_call_line=$(printf '%s\n' "$poll_body" | \
    grep -nF 'm6c2_publish_close();' | cut -d: -f1)
  required_line=$(printf '%s\n' "$poll_body" | \
    grep -n 'u32 required = M2C_ALL_DONE;' | cut -d: -f1)
  gate_line=$(printf '%s\n' "$poll_body" | \
    grep -n '__atomic_load_n(&completed.*required' | cut -d: -f1)
  pass_line=$(printf '%s\n' "$poll_body" | \
    grep -n 'QS:TEST_PASS:' | head -n 1 | cut -d: -f1)
  [ -n "$poll_call_line" ] && [ -n "$required_line" ] && \
    [ -n "$gate_line" ] && [ -n "$pass_line" ] && \
    [ "$poll_call_line" -lt "$required_line" ] && \
    [ "$poll_call_line" -lt "$gate_line" ] && \
    [ "$poll_call_line" -lt "$pass_line" ] || return 1

  publisher_body=$(function_body "$selftest" \
    'static void m6c2_publish_close(void)')
  printf '%s\n' "$publisher_body" | grep -q \
    'M6C2_CHILD_CLOSE_DONE | M6C2_LISTENER_CLOSE_DONE' || return 1
  claim_count=$(printf '%s\n' "$publisher_body" | \
    grep -c '__atomic_exchange_n(&m6c2_close_claimed' || true)
  print_count=$(printf '%s\n' "$publisher_body" | \
    grep -c 'QS:M6C2_CLOSE_OK' || true)
  done_count=$(printf '%s\n' "$publisher_body" | \
    grep -c 'M6C2_CLOSE_DONE' || true)
  [ "$claim_count" -eq 1 ] && [ "$print_count" -eq 1 ] && \
    [ "$done_count" -eq 1 ] || return 1
  printf '%s\n' "$publisher_body" | grep -Eq \
    '__atomic_exchange_n\(&m6c2_close_claimed, 1, __ATOMIC_(ACQ_REL|SEQ_CST)\)' || \
    return 1
  load_line=$(printf '%s\n' "$publisher_body" | \
    grep -n '__atomic_load_n(&completed' | cut -d: -f1)
  ready_line=$(printf '%s\n' "$publisher_body" | \
    grep -n '(value & required) != required' | cut -d: -f1)
  claim_line=$(printf '%s\n' "$publisher_body" | \
    grep -n '__atomic_exchange_n(&m6c2_close_claimed' | cut -d: -f1)
  print_line=$(printf '%s\n' "$publisher_body" | \
    grep -n 'QS:M6C2_CLOSE_OK' | cut -d: -f1)
  done_line=$(printf '%s\n' "$publisher_body" | \
    grep -n 'M6C2_CLOSE_DONE' | cut -d: -f1)
  [ -n "$load_line" ] && [ -n "$ready_line" ] && \
    [ -n "$claim_line" ] && [ -n "$print_line" ] && \
    [ -n "$done_line" ] && [ "$load_line" -lt "$ready_line" ] && \
    [ "$ready_line" -lt "$claim_line" ] && \
    [ "$claim_line" -lt "$print_line" ] && \
    [ "$print_line" -lt "$done_line" ]
}

check_close_contract "$root/kernel/src/selftest.c" || \
  fail 'M6C2 close selftest contract is incomplete'
awk '
  { print }
  /__atomic_fetch_or\(&completed, bit/ {
    print "    printk(\"bad\");"
    print "    __atomic_fetch_or(&completed, M6C2_CLOSE_DONE, __ATOMIC_RELEASE);"
  }
' "$root/kernel/src/selftest.c" > "$tmp/helper-final-done.c"
if check_close_contract "$tmp/helper-final-done.c"; then
  fail 'close contract checker accepted final evidence in generic helper'
fi
awk '
  /^void m2c_selftest_poll\(void\)$/ { poll = 1 }
  poll && /m6c2_publish_close\(\);/ { next }
  poll && /& required\) != required/ { after_gate = 1 }
  { print }
  poll && after_gate && /return;/ {
    print "    m6c2_publish_close();"
    after_gate = 0
  }
' "$root/kernel/src/selftest.c" > "$tmp/poll-late.c"
if check_close_contract "$tmp/poll-late.c"; then
  fail 'close contract checker accepted late poll publication'
fi
awk '
  /__atomic_exchange_n\(&m6c2_close_claimed/ { next }
  { print }
' "$root/kernel/src/selftest.c" > "$tmp/claim-missing.c"
if check_close_contract "$tmp/claim-missing.c"; then
  fail 'close contract checker accepted missing close claim'
fi
awk '
  /__atomic_exchange_n\(&m6c2_close_claimed/ { next }
  { print }
  /printk\("QS:M6C2_CLOSE_OK/ {
    print "    __atomic_exchange_n(&m6c2_close_claimed, 1, __ATOMIC_ACQ_REL);"
  }
' "$root/kernel/src/selftest.c" > "$tmp/claim-late.c"
if check_close_contract "$tmp/claim-late.c"; then
  fail 'close contract checker accepted claim after close print'
fi

echo 'PASS: M6C2 passive TCP contracts'
