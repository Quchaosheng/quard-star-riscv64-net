#include <timeros/os.h>

#ifndef QS_MIGRATION_TARGET
#define QS_MIGRATION_TARGET 100
#endif
#define SMP_SCHED_MIGRATIONS QS_MIGRATION_TARGET

static struct spinlock task_lock;
static int task_count;
static int nextpid;
static u64 migration_count;
static int migration_reported;
static int wait_reported;
static u32 sem_timeout_reported;

struct TaskControlBlock tasks[MAX_TASKS];

static void task_reset_wait(struct TaskControlBlock *p)
{
    p->wait_channel = 0;
    p->wait_deadline = WAIT_FOREVER;
    p->wait_result = 0;
    sem_init(&p->child_exit, 0);
}

static void task_first_run(void)
{
    TaskControlBlock *task = current_proc();
    if (task->kernel_entry != 0) {
        void (*entry)(void *) = task->kernel_entry;
        void *arg = task->kernel_arg;
        spin_unlock(&task_lock);
        entry(arg);
        panic("kernel task returned");
    }
    spin_unlock(&task_lock);
    if (task->pid == 0) {
        virtio_disk_smoke_test();
#ifdef QS_M3_TEST
        int result = fatfs_test_run();
        if (result != 0) {
            printk("QS:TEST_FAIL:m3-fatfs:%d\n", result);
            *(volatile u32 *)(uintptr_t)QEMU_TEST_BASE = QEMU_TEST_FAIL;
            for (;;)
                asm volatile("wfi");
        }
#endif
#ifdef QS_M4_TEST
        int net_result = virtio_net_raw_test();
        if (net_result != 0) {
            printk("QS:TEST_FAIL:m4-net-raw:%d\n", net_result);
            *(volatile u32 *)(uintptr_t)QEMU_TEST_BASE = QEMU_TEST_FAIL;
            for (;;)
                asm volatile("wfi");
        }
#endif
    }
    trap_return();
}

static struct TaskContext tcx_init(reg_t kstack_ptr)
{
    struct TaskContext task_ctx;

    memset(&task_ctx, 0, sizeof(task_ctx));
    task_ctx.ra = (reg_t)(uintptr_t)task_first_run;
    task_ctx.sp = kstack_ptr;
    return task_ctx;
}

static int allocpid_locked(void)
{
    return nextpid++;
}

int allocpid(void)
{
    spin_lock(&task_lock);
    int pid = allocpid_locked();
    spin_unlock(&task_lock);
    return pid;
}

void procinit(void)
{
    spin_init(&task_lock);
    task_count = 0;
    nextpid = 0;
    migration_count = 0;
    migration_reported = 0;
    wait_reported = 0;
    sem_timeout_reported = 0;

    for (struct TaskControlBlock *p = tasks; p < &tasks[MAX_TASKS]; p++) {
        p->task_state = UnInit;
        p->last_hart = -1;
        p->kernel_entry = 0;
        p->kernel_arg = 0;
        task_reset_wait(p);
    }
}

void proc_mapstacks(PageTable *kpgtbl)
{
    for (struct TaskControlBlock *p = tasks; p < &tasks[MAX_TASKS]; p++) {
        PhysPageNum ppn = kalloc();
        if (ppn.value == 0)
            panic("proc_mapstacks: out of memory");
        u64 va = KSTACK((int)(p - tasks));
        PageTable_map(kpgtbl, virt_addr_from_size_t(va),
                      phys_addr_from_phys_page_num(ppn), PAGE_SIZE,
                      PTE_R | PTE_W);
        p->kstack = va + PAGE_SIZE;
    }
}

static void proc_trap(struct TaskControlBlock *p)
{
    PhysPageNum ppn = kalloc();
    if (ppn.value == 0)
        panic("proc_trap: out of memory");
    p->trap_cx_ppn = phys_addr_from_phys_page_num(ppn).value;
    memset(&p->task_context, 0, sizeof(p->task_context));
}

extern char trampoline[];

static void proc_pagetable(struct TaskControlBlock *p)
{
    PageTable pagetable;
    pagetable.root_ppn = kalloc();
    if (pagetable.root_ppn.value == 0)
        panic("proc_pagetable: out of memory");

    PageTable_map(&pagetable, virt_addr_from_size_t(TRAMPOLINE),
                  phys_addr_from_size_t((u64)(uintptr_t)trampoline), PAGE_SIZE,
                  PTE_R | PTE_X);
    PageTable_map(&pagetable, virt_addr_from_size_t(TRAPFRAME),
                  phys_addr_from_size_t(p->trap_cx_ppn), PAGE_SIZE,
                  PTE_R | PTE_W);
    p->pagetable = pagetable;
}

void proc_ustack(struct TaskControlBlock *p)
{
    PhysPageNum ppn = kalloc();
    if (ppn.value == 0)
        panic("proc_ustack: out of memory");
    u64 paddr = phys_addr_from_phys_page_num(ppn).value;
    PageTable_map(&p->pagetable,
                  virt_addr_from_size_t(p->ustack - PAGE_SIZE),
                  phys_addr_from_size_t(paddr), PAGE_SIZE,
                  PTE_R | PTE_W | PTE_U);
}

TaskControlBlock *task_create_pt(size_t app_id)
{
    if (app_id >= MAX_TASKS)
        panic("task_create_pt: invalid app id");

    struct TaskControlBlock *p = &tasks[app_id];
    spin_lock(&task_lock);
    if (p->task_state != UnInit) {
        spin_unlock(&task_lock);
        panic("task_create_pt: task already used");
    }
    p->task_state = Creating;
    p->last_hart = -1;
    task_reset_wait(p);
    spin_unlock(&task_lock);

    proc_trap(p);
    proc_pagetable(p);
    return p;
}

extern u64 kernel_satp;

void app_init(size_t app_id)
{
    struct TaskControlBlock *p = &tasks[app_id];
    TrapContext *cx_ptr = (TrapContext *)(uintptr_t)p->trap_cx_ppn;
    reg_t sstatus = r_sstatus();
    sstatus &= ~SSTATUS_SPP;
    sstatus |= SSTATUS_SPIE;

    cx_ptr->sepc = p->entry;
    cx_ptr->sstatus = sstatus;
    cx_ptr->sp = p->ustack;
    cx_ptr->kernel_satp = kernel_satp;
    cx_ptr->kernel_sp = p->kstack;
    cx_ptr->trap_handler = (u64)(uintptr_t)trap_handler;
    p->task_context = tcx_init((reg_t)p->kstack);

    spin_lock(&task_lock);
    if (p->task_state != Creating) {
        spin_unlock(&task_lock);
        panic("app_init: task not reserved");
    }
    p->pid = allocpid_locked();
    p->task_state = Ready;
    task_count++;
    spin_unlock(&task_lock);
}

int task_create_kernel(void (*entry)(void *), void *arg)
{
    if (entry == 0)
        return -1;
    TaskControlBlock *task = 0;
    spin_lock(&task_lock);
    for (TaskControlBlock *candidate = tasks;
         candidate < &tasks[MAX_TASKS]; candidate++) {
        if (candidate->task_state == UnInit) {
            task = candidate;
            task->task_state = Creating;
            task->pid = allocpid_locked();
            task->parent = 0;
            task->kernel_entry = entry;
            task->kernel_arg = arg;
            task_reset_wait(task);
            task->task_context = tcx_init((reg_t)task->kstack);
            task->task_state = Ready;
            task_count++;
            break;
        }
    }
    spin_unlock(&task_lock);
    return task != 0 ? 0 : -1;
}

struct TaskControlBlock *current_proc(void)
{
    struct TaskControlBlock *p = cpu_this()->proc;
    if (p == 0)
        panic("current_proc: no running process");
    return p;
}

u64 get_current_trap_cx(void)
{
    return current_proc()->trap_cx_ppn;
}

u64 current_user_token(void)
{
    return MAKE_SATP(current_proc()->pagetable.root_ppn.value);
}

static int other_cpu_idle(u64 hartid)
{
    for (int i = 0; i < MAX_CPUS; i++) {
        if ((u64)i != hartid && cpus[i].present && cpus[i].online &&
            cpus[i].idle)
            return 1;
    }
    return 0;
}

static struct TaskControlBlock *pick_runnable(struct cpu *cpu)
{
    struct TaskControlBlock *fallback = 0;

    for (struct TaskControlBlock *p = tasks; p < &tasks[MAX_TASKS]; p++) {
        if (p->task_state != Ready)
            continue;
        if (p->last_hart >= 0 && (u64)p->last_hart != cpu->hartid)
            return p;
        if (fallback == 0)
            fallback = p;
    }

    if (fallback != 0 && fallback->last_hart == (int)cpu->hartid &&
        other_cpu_idle(cpu->hartid))
        return 0;
    return fallback;
}

static int task_wake_locked(void *channel, int wake_all)
{
    int woken = 0;

    for (struct TaskControlBlock *p = tasks; p < &tasks[MAX_TASKS]; p++) {
        if (p->task_state != Sleeping || p->wait_channel != channel)
            continue;
        p->wait_result = 0;
        p->task_state = Ready;
        woken++;
        if (!wake_all)
            break;
    }

    if (woken != 0) {
        u64 mask = 0;
        struct cpu *self = cpu_this();
        for (int i = 0; i < MAX_CPUS; i++) {
            if (&cpus[i] != self && cpus[i].present && cpus[i].online &&
                cpus[i].idle)
                mask |= 1ULL << cpus[i].hartid;
        }
        if (mask != 0) {
            struct sbiret ret = sbi_send_ipi(mask, 0);
            if (ret.error != 0)
                panic("task_wake: SBI IPI failed");
        }
    }
    return woken;
}

static void task_wake_expired_locked(u64 now)
{
    for (struct TaskControlBlock *p = tasks; p < &tasks[MAX_TASKS]; p++) {
        if (p->task_state == Sleeping && p->wait_deadline != WAIT_FOREVER &&
            p->wait_deadline <= now) {
            p->wait_result = -1;
            p->task_state = Ready;
        }
    }
}

static void record_migration(struct TaskControlBlock *p, struct cpu *cpu)
{
    if (p->last_hart >= 0 && (u64)p->last_hart != cpu->hartid) {
        migration_count++;
        if (!migration_reported && migration_count >= SMP_SCHED_MIGRATIONS) {
            migration_reported = 1;
            printk("QS:SMP_SCHED_OK\n");
            printk("QS:STRESS_MIGRATIONS:%d\n", (int)migration_count);
            printk("QS:TEST_PASS:m2b-smoke\n");
            m2c_mark_sched();
        }
    }
    p->last_hart = (int)cpu->hartid;
}

void scheduler(void)
{
    struct cpu *cpu = cpu_this();
    cpu->proc = 0;

    for (;;) {
        intr_on();
        m2c_selftest_poll();
        spin_lock(&task_lock);
        task_wake_expired_locked(r_mtime());

        struct TaskControlBlock *p = pick_runnable(cpu);
        if (p == 0) {
            cpu->idle = 1;
            spin_unlock(&task_lock);
            asm volatile("wfi");
            continue;
        }

        cpu->idle = 0;
        record_migration(p, cpu);
        p->task_state = Running;
        cpu->proc = p;
        __atomic_store_n(&cpu->need_resched, 0, __ATOMIC_RELAXED);
        __switch(&cpu->scheduler_context, &p->task_context);
        cpu->proc = 0;
        spin_unlock(&task_lock);
    }
}

void schedule(void)
{
    struct cpu *cpu = cpu_this();
    struct TaskControlBlock *p = current_proc();

    spin_lock(&task_lock);
    if (p->task_state != Running) {
        spin_unlock(&task_lock);
        panic("schedule: process is not running");
    }
    p->task_state = Ready;
    __atomic_store_n(&cpu->need_resched, 0, __ATOMIC_RELAXED);
    __switch(&p->task_context, &cpu->scheduler_context);
    spin_unlock(&task_lock);
}

int task_sleep(void *channel, struct spinlock *caller_lock, u64 deadline)
{
    struct cpu *cpu = cpu_this();
    struct TaskControlBlock *p = current_proc();

    spin_lock(&task_lock);
    spin_unlock(caller_lock);
    if (p->task_state != Running) {
        spin_unlock(&task_lock);
        panic("task_sleep: process is not running");
    }

    p->wait_channel = channel;
    p->wait_deadline = deadline;
    p->wait_result = 0;
    p->task_state = Sleeping;
    __switch(&p->task_context, &cpu->scheduler_context);

    int result = p->wait_result;
    p->wait_channel = 0;
    p->wait_deadline = WAIT_FOREVER;
    spin_unlock(&task_lock);
    spin_lock(caller_lock);
    return result;
}

int task_wake(void *channel, int wake_all)
{
    spin_lock(&task_lock);
    int woken = task_wake_locked(channel, wake_all);
    spin_unlock(&task_lock);
    return woken;
}

void run_first_task(void)
{
    scheduler();
}

struct TaskControlBlock *allocproc(void)
{
    struct TaskControlBlock *p = 0;

    spin_lock(&task_lock);
    for (struct TaskControlBlock *candidate = tasks;
         candidate < &tasks[MAX_TASKS]; candidate++) {
        if (candidate->task_state == UnInit) {
            p = candidate;
            p->task_state = Creating;
            p->pid = allocpid_locked();
            p->last_hart = -1;
            task_reset_wait(p);
            break;
        }
    }
    spin_unlock(&task_lock);

    if (p == 0)
        return 0;
    proc_trap(p);
    proc_pagetable(p);
    return p;
}

int __sys_fork(void)
{
    struct TaskControlBlock *p = current_proc();
    struct TaskControlBlock *np = allocproc();
    if (np == 0)
        return -1;

    uvmcopy(&p->pagetable, &np->pagetable, p->base_size);
    memcpy((void *)(uintptr_t)np->trap_cx_ppn,
           (void *)(uintptr_t)p->trap_cx_ppn, PAGE_SIZE);

    TrapContext *cx_ptr = (TrapContext *)(uintptr_t)np->trap_cx_ppn;
    cx_ptr->a0 = 0;
    cx_ptr->kernel_sp = np->kstack;
    np->entry = p->entry;
    np->base_size = p->base_size;
    np->ustack = p->ustack;
    np->task_context = tcx_init((reg_t)np->kstack);

    spin_lock(&task_lock);
    np->parent = p;
    np->task_state = Ready;
    task_count++;
    int pid = np->pid;
    spin_unlock(&task_lock);
    return pid;
}

int exec(const char *name)
{
    AppMetadata metadata = get_app_data_by_name(name);
    if (metadata.id < 0)
        return -1;

    elf64_ehdr_t *ehdr = (elf64_ehdr_t *)(uintptr_t)metadata.start;
    elf_check(ehdr);
    struct TaskControlBlock *proc = current_proc();
    PageTable old_pagetable = proc->pagetable;
    u64 oldsz = proc->base_size;

    proc_pagetable(proc);
    load_segment(ehdr, proc);
    proc_ustack(proc);

    TrapContext *cx_ptr = (TrapContext *)(uintptr_t)proc->trap_cx_ppn;
    cx_ptr->sepc = (u64)ehdr->e_entry;
    cx_ptr->sp = proc->ustack;
    proc_freepagetable(&old_pagetable, oldsz);
    return 0;
}

static void freeproc_locked(struct TaskControlBlock *p)
{
    proc_freepagetable(&p->pagetable, p->base_size);
    if (p->trap_cx_ppn != 0) {
        kfree(floor_phys(phys_addr_from_size_t(p->trap_cx_ppn)));
        p->trap_cx_ppn = 0;
    }

    p->pagetable.root_ppn.value = 0;
    p->base_size = 0;
    p->parent = 0;
    p->ustack = 0;
    p->entry = 0;
    p->kernel_entry = 0;
    p->kernel_arg = 0;
    p->exit_code = 0;
    p->last_hart = -1;
    p->wait_channel = 0;
    p->wait_deadline = WAIT_FOREVER;
    p->wait_result = 0;
    p->task_state = UnInit;
}

void freeproc(struct TaskControlBlock *p)
{
    spin_lock(&task_lock);
    freeproc_locked(p);
    spin_unlock(&task_lock);
}

static void children_proc_clear_locked(struct TaskControlBlock *p)
{
    for (struct TaskControlBlock *child = tasks;
         child < &tasks[MAX_TASKS]; child++) {
        if (child->parent == p)
            child->parent = &tasks[0];
    }
}

static struct semaphore *lock_parent_exit(struct TaskControlBlock *p)
{
    for (;;) {
        spin_lock(&task_lock);
        struct TaskControlBlock *parent = p->parent;
        spin_unlock(&task_lock);

        struct semaphore *parent_exit = &parent->child_exit;
        spin_lock(&parent_exit->lock);
        spin_lock(&task_lock);
        if (p->parent == parent)
            return parent_exit;
        spin_unlock(&task_lock);
        spin_unlock(&parent_exit->lock);
    }
}

void exit_current_and_run_next(u64 exit_code)
{
    struct cpu *cpu = cpu_this();
    struct TaskControlBlock *p = current_proc();
    if (p->pid == 0)
        panic("init exiting");

    struct semaphore *parent_exit = lock_parent_exit(p);
    p->exit_code = exit_code;
    p->task_state = Zombie;
    children_proc_clear_locked(p);
    task_count--;
    parent_exit->count++;
    if (parent_exit->wait.waiters != 0)
        task_wake_locked(&parent_exit->wait, 0);
    spin_unlock(&parent_exit->lock);
    __switch(&p->task_context, &cpu->scheduler_context);
    panic("zombie exit");
}

int wait(void)
{
    struct TaskControlBlock *p = current_proc();

    for (;;) {
        int havekids = 0;
        spin_lock(&task_lock);
        for (struct TaskControlBlock *child = tasks;
             child < &tasks[MAX_TASKS]; child++) {
            if (child->parent != p)
                continue;
            havekids = 1;
            if (child->task_state == Zombie) {
                int pid = child->pid;
                freeproc_locked(child);
                spin_unlock(&task_lock);
#ifdef QS_M2C_TEST
                if (!__atomic_exchange_n(&wait_reported, 1,
                                         __ATOMIC_ACQ_REL)) {
                    printk("QS:WAIT_OK\n");
                    m2c_mark_wait();
                }
#endif
                return pid;
            }
        }
        spin_unlock(&task_lock);

        if (!havekids)
            return -1;
#ifdef QS_M2C_TEST
        if (sem_timedwait(&p->child_exit, r_mtime() + 100000ULL) < 0 &&
            !__atomic_exchange_n(&sem_timeout_reported, 1,
                                 __ATOMIC_ACQ_REL))
            printk("QS:SEM_TIMEOUT_OK\n");
#else
        sem_wait(&p->child_exit);
#endif
    }
}
