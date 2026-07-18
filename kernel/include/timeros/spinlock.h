#ifndef TOS_SPINLOCK_H__
#define TOS_SPINLOCK_H__

#include <timeros/types.h>

struct cpu;

struct spinlock {
    u32 locked;
    struct cpu *owner;
};

void spin_init(struct spinlock *lock);
void spin_lock(struct spinlock *lock);
void spin_unlock(struct spinlock *lock);
int spin_holding(struct spinlock *lock);

#endif
