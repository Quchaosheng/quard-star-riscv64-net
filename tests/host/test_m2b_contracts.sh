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

require_absence() {
  file=$1
  text=$2
  message=$3
  if grep -Fq "$text" "$root/$file" 2>/dev/null; then
    echo "FAIL: $message" >&2
    status=1
  fi
}

require_text kernel/include/timeros/cpu.h \
  'struct TaskControlBlock *proc;' 'each CPU must own its current process'
require_text kernel/include/timeros/cpu.h \
  'struct TaskContext scheduler_context;' 'each CPU needs a scheduler context'
require_text kernel/include/timeros/cpu.h \
  'u32 idle;' 'each CPU must publish scheduler idle state'
require_text kernel/include/timeros/cpu.h \
  'u32 need_resched;' 'timer reschedule state must be per-CPU'
require_text kernel/include/timeros/cpu.h \
  'u64 timer_ticks;' 'timer tick state must be per-CPU'
require_text kernel/include/timeros/context.h \
  'reg_t kernel_tp;' 'the user trap frame must carry the destination CPU pointer'
require_text kernel/src/kerneltrap.S \
  'ld tp, 37*8(sp)' 'user trap entry must restore the kernel CPU pointer'
require_text kernel/src/task.c \
  'static struct spinlock task_lock;' 'the shared runnable set needs a lock'
require_text kernel/src/task.c \
  'void scheduler(void)' 'each online hart needs a scheduler loop'
require_text kernel/src/task.c \
  '__switch(&cpu->scheduler_context' 'processes must switch through per-CPU scheduler state'
require_text kernel/src/task.c \
  'printk("QS:SMP_SCHED_OK\n");' 'migration completion needs a stable marker'
require_text kernel/src/timer.c \
  '__ATOMIC_RELEASE' 'timer reschedule publication must use release ordering'
require_text kernel/src/trap.c \
  'timer_tick();' 'timer interrupts must only record a tick and re-arm'
require_text kernel/src/trap.c \
  'cpu_take_resched()' 'user trap return must consume deferred rescheduling'
require_absence kernel/src/trap.c \
  'if (from_user) {' 'interrupt dispatch must not switch contexts directly'
require_text scripts/m2b-build.sh \
  'export QS_STAGE=m2b' 'M2B must have an isolated build output'
require_text scripts/m2b-smoke.sh \
  'QS:TEST_PASS:m2b-smoke' 'M2B smoke must require its stage marker'

if [ "$status" -ne 0 ]; then
  exit "$status"
fi

echo "PASS: M2B source contracts"
