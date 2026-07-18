#include <timeros/os.h>

static void push_off(void)
{
    int enabled = intr_get();
    intr_off();

    struct cpu *cpu = cpu_this();
    if (cpu->noff == 0)
        cpu->intena = enabled;
    cpu->noff++;
}

static void pop_off(void)
{
    struct cpu *cpu = cpu_this();
    if (intr_get() || cpu->noff < 1)
        panic("pop_off");

    cpu->noff--;
    if (cpu->noff == 0 && cpu->intena)
        intr_on();
}

void spin_init(struct spinlock *lock)
{
    lock->locked = 0;
    __atomic_store_n(&lock->owner, 0, __ATOMIC_RELAXED);
}

int spin_holding(struct spinlock *lock)
{
    return __atomic_load_n(&lock->locked, __ATOMIC_RELAXED) &&
           __atomic_load_n(&lock->owner, __ATOMIC_RELAXED) == cpu_this();
}

void spin_lock(struct spinlock *lock)
{
    push_off();
    if (spin_holding(lock))
        panic("spin_lock: recursive acquire");

    while (__atomic_exchange_n(&lock->locked, 1, __ATOMIC_ACQUIRE)) {
        asm volatile("nop");
    }
    __atomic_store_n(&lock->owner, cpu_this(), __ATOMIC_RELAXED);
}

void spin_unlock(struct spinlock *lock)
{
    if (!spin_holding(lock))
        panic("spin_unlock: not owner");

    __atomic_store_n(&lock->owner, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&lock->locked, 0, __ATOMIC_RELEASE);
    pop_off();
}
