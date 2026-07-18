#ifndef TOS_TASK_H__
#define TOS_TASK_H__

#include <timeros/os.h>

#define MAX_TASKS 10

typedef enum TaskState {
    UnInit,
    Creating,
    Ready,
    Running,
    Sleeping,
    Zombie,
} TaskState;

typedef struct TaskControlBlock {
    TaskState task_state;
    int pid;
    struct TaskControlBlock *parent;
    TaskContext task_context;
    u64 trap_cx_ppn;
    u64 base_size;
    u64 kstack;
    u64 ustack;
    u64 entry;
    PageTable pagetable;
    u64 exit_code;
    int last_hart;
    void *wait_channel;
    u64 wait_deadline;
    int wait_result;
    struct semaphore child_exit;
} TaskControlBlock;

void proc_mapstacks(PageTable *kpgtbl);
void procinit(void);
int allocpid(void);
TaskControlBlock *task_create_pt(size_t app_id);
void app_init(size_t app_id);
u64 get_current_trap_cx(void);
u64 current_user_token(void);
struct TaskControlBlock *current_proc(void);
void schedule(void);
void scheduler(void);
int task_sleep(void *channel, struct spinlock *caller_lock, u64 deadline);
int task_wake(void *channel, int wake_all);
void run_first_task(void);
void proc_ustack(struct TaskControlBlock *p);
int __sys_fork(void);
int exec(const char *name);
void exit_current_and_run_next(u64 exit_code);
void freeproc(struct TaskControlBlock *p);
int wait(void);

#endif
