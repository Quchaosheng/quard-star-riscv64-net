#ifndef TOS_OS_H__
#define TOS_OS_H__


#include <stdarg.h>
#include <timeros/types.h>
#include <timeros/address.h>
#include <timeros/assert.h>
#include <timeros/context.h>
#include <timeros/cpu.h>
#include <timeros/loader.h>
#include <timeros/memory.h>
#include <timeros/plic.h>
#include <timeros/riscv.h>
#include <timeros/sbi.h>
#include <timeros/selftest.h>
#include <timeros/fatfs_test.h>
#include <timeros/spinlock.h>
#include <timeros/wait.h>
#include <timeros/stdio.h>
#include <timeros/string.h>
#include <timeros/syscall.h>
#include <timeros/task.h>
#include <timeros/virtio.h>
#include <timeros/virtio_net.h>
#include <timeros/virtio_net_completion.h>
#include <timeros/bio.h>

/* trap.c */
extern void trap_handler();
extern void trap_return();
extern void set_kernel_trap_entry();
extern void kerneltrap();
/* kerneltrap.S*/
extern void __alltraps(void);
extern void __restore(TrapContext *next);
extern void kernelvec(void);


/* switch.S */
extern void __switch(TaskContext *current_task_cx_ptr,TaskContext* next_task_cx_ptr );

/* timer.c */
void sbi_set_timer(uint64_t stime_value);
void timer_init(void);
void set_next_trigger(void);
void timer_tick(void);
int cpu_take_resched(void);
extern uint64_t get_time_us();

#endif /* __OS_H__ */
