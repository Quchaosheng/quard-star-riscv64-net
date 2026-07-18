#include <timeros/types.h>

struct spinlock {
    u32 locked;
    void *owner;
};

void spin_init(struct spinlock *lock)
{
    lock->locked = 0;
    lock->owner = 0;
}

void spin_lock(struct spinlock *lock)
{
    while (__atomic_exchange_n(&lock->locked, 1, __ATOMIC_ACQUIRE))
        ;
}

void spin_unlock(struct spinlock *lock)
{
    __atomic_store_n(&lock->locked, 0, __ATOMIC_RELEASE);
}
