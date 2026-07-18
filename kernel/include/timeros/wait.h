#ifndef TOS_WAIT_H__
#define TOS_WAIT_H__

#include <timeros/spinlock.h>
#include <timeros/types.h>

#define WAIT_FOREVER (~0ULL)

struct wait_queue {
    u32 waiters;
};

struct semaphore {
    struct spinlock lock;
    int count;
    struct wait_queue wait;
};

struct sleeplock {
    struct spinlock lock;
    int locked;
    int owner_pid;
    struct wait_queue wait;
};

void wait_queue_init(struct wait_queue *wait);
void sem_init(struct semaphore *sem, int count);
int sem_wait(struct semaphore *sem);
int sem_timedwait(struct semaphore *sem, u64 deadline);
void sem_post(struct semaphore *sem);
void sleeplock_init(struct sleeplock *lock);
void sleeplock_acquire(struct sleeplock *lock);
void sleeplock_release(struct sleeplock *lock);
int sleeplock_holding(struct sleeplock *lock);

#endif
