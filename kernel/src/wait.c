#include <timeros/os.h>

void wait_queue_init(struct wait_queue *wait)
{
    wait->waiters = 0;
}

void sem_init(struct semaphore *sem, int count)
{
    if (count < 0)
        panic("sem_init: negative count");
    spin_init(&sem->lock);
    sem->count = count;
    wait_queue_init(&sem->wait);
}

int sem_timedwait(struct semaphore *sem, u64 deadline)
{
    spin_lock(&sem->lock);
    while (sem->count == 0) {
        sem->wait.waiters++;
        int result = task_sleep(&sem->wait, &sem->lock, deadline);
        sem->wait.waiters--;
        if (result < 0) {
            spin_unlock(&sem->lock);
            return -1;
        }
    }
    sem->count--;
    spin_unlock(&sem->lock);
    return 0;
}

int sem_wait(struct semaphore *sem)
{
    return sem_timedwait(sem, WAIT_FOREVER);
}

void sem_post(struct semaphore *sem)
{
    spin_lock(&sem->lock);
    sem->count++;
    if (sem->wait.waiters != 0)
        task_wake(&sem->wait, 0);
    spin_unlock(&sem->lock);
}

void sleeplock_init(struct sleeplock *lock)
{
    spin_init(&lock->lock);
    lock->locked = 0;
    lock->owner_pid = -1;
    wait_queue_init(&lock->wait);
}

void sleeplock_acquire(struct sleeplock *lock)
{
    spin_lock(&lock->lock);
    while (lock->locked) {
        lock->wait.waiters++;
        task_sleep(&lock->wait, &lock->lock, WAIT_FOREVER);
        lock->wait.waiters--;
    }
    lock->locked = 1;
    lock->owner_pid = current_proc()->pid;
    spin_unlock(&lock->lock);
}

void sleeplock_release(struct sleeplock *lock)
{
    spin_lock(&lock->lock);
    if (!lock->locked || lock->owner_pid != current_proc()->pid) {
        spin_unlock(&lock->lock);
        panic("sleeplock_release: not owner");
    }
    lock->locked = 0;
    lock->owner_pid = -1;
    if (lock->wait.waiters != 0)
        task_wake(&lock->wait, 0);
    spin_unlock(&lock->lock);
}

int sleeplock_holding(struct sleeplock *lock)
{
    spin_lock(&lock->lock);
    int holding = lock->locked && lock->owner_pid == current_proc()->pid;
    spin_unlock(&lock->lock);
    return holding;
}
