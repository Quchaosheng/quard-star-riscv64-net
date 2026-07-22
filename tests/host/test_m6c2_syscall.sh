#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
header="$root/kernel/include/timeros/syscall.h"
app="$root/kernel/lib/app.c"
kernel="$root/kernel/src/syscall.c"

fail()
{
  echo "FAIL: $*" >&2
  exit 1
}

strip_c_comments()
{
  awk '
    function strip(line, out, block_start, line_start, block_end) {
      out = ""
      while (length(line)) {
        if (in_block) {
          block_end = index(line, "*/")
          if (!block_end)
            return out
          line = substr(line, block_end + 2)
          in_block = 0
          continue
        }
        block_start = index(line, "/*")
        line_start = index(line, "//")
        if (line_start && (!block_start || line_start < block_start))
          return out substr(line, 1, line_start - 1)
        if (!block_start)
          return out line
        out = out substr(line, 1, block_start - 1)
        line = substr(line, block_start + 2)
        in_block = 1
      }
      return out
    }
    { print strip($0) }
  ' "$1"
}

check_accept_order()
{
  strip_c_comments "$1" | awk '
    function count(text, token, total, offset) {
      total = 0
      while ((offset = index(text, token)) != 0) {
        total++
        text = substr(text, offset + length(token))
      }
      return total
    }
    function pos(text, token) {
      return index(text, token)
    }
    function nth_pos(text, token, wanted, found, offset, at) {
      offset = 1
      for (found = 1; found <= wanted; found++) {
        at = index(substr(text, offset), token)
        if (!at)
          return 0
        at += offset - 1
        offset = at + length(token)
      }
      return at
    }
    {
      if (!inside &&
          $0 ~ /static[[:space:]]+int[[:space:]]+__sys_accept[[:space:]]*\(/) {
        inside = 1
        functions++
        depth = 0
        body_started = 0
      }
      if (!inside)
        next
      body = body $0
      braces = $0
      opens = gsub(/{/, "", braces)
      braces = $0
      closes = gsub(/}/, "", braces)
      if (opens)
        body_started = 1
      depth += opens - closes
      if (body_started && depth == 0)
        inside = 0
    }
    END {
      gsub(/[[:space:]]/, "", body)
      pair = "(user_address==0)!=(user_address_length==0)"
      copy_in = "copy_from_user(&address_length,(constchar*)user_address_length,sizeof(address_length))"
      length_check = "address_length<sizeof(net_sockaddr_in)"
      address_range = "user_range_check((constchar*)user_address,sizeof(net_sockaddr_in),PTE_W)"
      length_range = "user_range_check((constchar*)user_address_length,sizeof(address_length),PTE_W)"
      prepare = "net_socket_accept_prepare(handle,&accept)"
      address_copy = "copy_to_user((char*)user_address,&address,sizeof(address))"
      length_copy = "copy_to_user((char*)user_address_length,&address_length,sizeof(address_length))"
      abort_call = "net_socket_accept_abort(&accept)"
      commit = "net_socket_accept_commit(&accept)"
      if (functions != 1 || inside || count(body, pair) != 1 ||
          count(body, copy_in) != 1 || count(body, length_check) != 1 ||
          count(body, address_range) != 1 || count(body, length_range) != 1 ||
          count(body, prepare) != 1 || count(body, address_copy) != 1 ||
          count(body, length_copy) != 1 || count(body, abort_call) != 2 ||
          count(body, commit) != 1)
        exit 1
      p0 = pos(body, pair)
      p1 = pos(body, copy_in)
      p2 = pos(body, length_check)
      p3 = pos(body, address_range)
      p4 = pos(body, length_range)
      p5 = pos(body, prepare)
      p6 = pos(body, address_copy)
      p7 = nth_pos(body, abort_call, 1)
      p8 = pos(body, length_copy)
      p9 = nth_pos(body, abort_call, 2)
      p10 = pos(body, commit)
      exit !(p0 < p1 && p1 < p2 && p2 < p3 && p3 < p4 && p4 < p5 &&
             p5 < p6 && p6 < p7 && p7 < p8 && p8 < p9 && p9 < p10)
    }
  '
}

grep -qx '#define __NR_listen 201' "$header" || fail 'missing __NR_listen 201'
grep -qx '#define __NR_accept 202' "$header" || fail 'missing __NR_accept 202'
grep -q '^int sys_listen(int fd, int backlog);$' "$header" || \
  fail 'missing user listen declaration'
grep -q '^int sys_accept(int fd, net_sockaddr_in \*address, size_t \*address_length);$' \
  "$header" || fail 'missing user accept declaration'
grep -q '^int sys_listen(int fd, int backlog)$' "$app" || \
  fail 'missing user listen wrapper'
grep -q 'syscall(__NR_listen, fd, backlog, 0)' "$app" || \
  fail 'listen wrapper has the wrong ABI'
grep -q '^int sys_accept(int fd, net_sockaddr_in \*address, size_t \*address_length)$' \
  "$app" || fail 'missing user accept wrapper'
grep -q 'syscall(__NR_accept, fd, (reg_t)(uintptr_t)address,' "$app" || \
  fail 'accept wrapper must pass the address pointer'
grep -q '(reg_t)(uintptr_t)address_length)' "$app" || \
  fail 'accept wrapper must pass the address length pointer'
grep -q 'case __NR_listen:' "$kernel" || fail 'missing listen dispatch'
grep -q 'case __NR_accept:' "$kernel" || fail 'missing accept dispatch'

grep -q 'SOCKET_EXEC_LISTEN' "$kernel" || fail 'missing listen worker operation'
awk '
  /static void socket_exec\(void \*arg\)/ { inside = 1 }
  inside && /SOCKET_EXEC_LISTEN/ { operation = 1 }
  inside && /net_socket_listen\(request->handle, request->length\)/ { call = 1 }
  /static int socket_exec_wait/ { inside = 0 }
  END { exit !(operation && call) }
' "$kernel" || fail 'listen must execute through socket_exec'
strip_c_comments "$kernel" | awk '
  /static void socket_exec\(void \*arg\)/ { inside = 1 }
  /static int socket_exec_wait/ { inside = 0 }
  inside && /net_socket_accept_/ { found = 1 }
  END { exit found }
' || fail 'accept must not execute through socket_exec'
awk '
  /static int __sys_listen\(/ { inside = 1 }
  inside && /\.op = SOCKET_EXEC_LISTEN/ { operation = 1 }
  inside && /\.length = backlog/ { backlog = 1 }
  inside && /socket_exec_wait\(&request\)/ { wait = 1 }
  inside && /^}/ { inside = 0 }
  END { exit !(operation && backlog && wait) }
' "$kernel" || fail 'listen syscall must submit backlog through socket_exec_wait'

check_accept_order "$kernel" || fail 'accept validation and commit order is unsafe'

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cat > "$tmp/comments.c" <<'EOF'
static int __sys_accept(void) {
  /* (user_address == 0) != (user_address_length == 0)
     copy_from_user(&address_length, (const char *)user_address_length, sizeof(address_length))
     address_length < sizeof(net_sockaddr_in)
     user_range_check((const char *)user_address, sizeof(net_sockaddr_in), PTE_W)
     user_range_check((const char *)user_address_length, sizeof(address_length), PTE_W)
     net_socket_accept_prepare(handle, &accept) */
  // copy_to_user((char *)user_address, &address, sizeof(address))
  // net_socket_accept_abort(&accept)
  // copy_to_user((char *)user_address_length, &address_length, sizeof(address_length))
  // net_socket_accept_abort(&accept)
  // net_socket_accept_commit(&accept)
  return NET_ERR_PARAM;
}
EOF
if check_accept_order "$tmp/comments.c"; then
  fail 'accept checker must ignore ordered comments'
fi

cat > "$tmp/early-commit.c" <<'EOF'
static int __sys_accept(void) {
  (user_address == 0) != (user_address_length == 0);
  copy_from_user(&address_length, (const char *)user_address_length, sizeof(address_length));
  address_length < sizeof(net_sockaddr_in);
  user_range_check((const char *)user_address, sizeof(net_sockaddr_in), PTE_W);
  user_range_check((const char *)user_address_length, sizeof(address_length), PTE_W);
  net_socket_accept_prepare(handle, &accept);
  net_socket_accept_commit(&accept);
  copy_to_user((char *)user_address, &address, sizeof(address));
  net_socket_accept_abort(&accept);
  copy_to_user((char *)user_address_length, &address_length, sizeof(address_length));
  net_socket_accept_abort(&accept);
}
EOF
if check_accept_order "$tmp/early-commit.c"; then
  fail 'accept checker must reject an early commit'
fi

echo 'PASS: M6C2 listen and accept syscall contracts'
