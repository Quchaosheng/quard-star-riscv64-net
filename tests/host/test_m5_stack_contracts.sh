#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
status=0

require_text() {
  file=$1
  text=$2
  message=$3
  if ! grep -Fq "$text" "$root/$file" 2>/dev/null; then
    echo "FAIL: $message" >&2
    status=1
  fi
}

require_text kernel/include/timeros/task.h \
  'int task_create_kernel(void (*entry)(void *), void *arg);' \
  'M5 needs a schedulable kernel worker task'
require_text kernel/src/task.c 'kernel_entry' \
  'kernel tasks need a separate entry path'
require_text kernel/src/task.c 'task_create_kernel' \
  'kernel worker creation must enter the task table'
require_text kernel/include/timeros/net/net_stack.h 'net_stack_worker' \
  'M5 needs a network worker entry'
require_text kernel/src/main.c 'net_stack_init()' \
  'boot must initialize the M5 stack'
require_text kernel/src/main.c 'task_create_kernel(net_stack_worker' \
  'boot must schedule the M5 network worker'
require_text kernel/src/trap.c 'QS_M5_TEST' \
  'M5 must dispatch VirtIO-net interrupts'

if [ "$status" -ne 0 ]; then
  exit "$status"
fi
echo 'PASS: M5 network stack integration contracts'
