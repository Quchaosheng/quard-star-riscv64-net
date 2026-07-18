#define _POSIX_C_SOURCE 200809L

#include <timeros/net/net_sys.h>

#include <limits.h>

#ifdef __riscv

#include <timeros/riscv.h>
#include <timeros/spinlock.h>
#include <timeros/wait.h>

#define NET_SYS_SEM_MAX 32
#define MTIME_TICKS_PER_MS 10000ULL

struct net_sys_sem {
    struct semaphore sem;
    int used;
};

static struct spinlock sem_pool_lock;
static struct net_sys_sem sem_pool[NET_SYS_SEM_MAX];

static int sem_pool_index(sys_sem_t sem)
{
    for (int i = 0; i < NET_SYS_SEM_MAX; i++) {
        if (sem == &sem_pool[i])
            return i;
    }
    return -1;
}

net_err_t net_sys_init(void)
{
    spin_init(&sem_pool_lock);
    for (int i = 0; i < NET_SYS_SEM_MAX; i++)
        sem_pool[i].used = 0;
    return NET_ERR_OK;
}

void sys_time_curr(net_time_t *time)
{
    if (time != 0)
        *time = r_mtime() / MTIME_TICKS_PER_MS;
}

sys_sem_t sys_sem_create(int initial_count)
{
    if (initial_count < 0)
        return SYS_SEM_INVALID;

    spin_lock(&sem_pool_lock);
    for (int i = 0; i < NET_SYS_SEM_MAX; i++) {
        if (!sem_pool[i].used) {
            sem_pool[i].used = 1;
            sem_init(&sem_pool[i].sem, initial_count);
            spin_unlock(&sem_pool_lock);
            return &sem_pool[i];
        }
    }
    spin_unlock(&sem_pool_lock);
    return SYS_SEM_INVALID;
}

void sys_sem_free(sys_sem_t sem)
{
    if (sem == SYS_SEM_INVALID)
        return;

    spin_lock(&sem_pool_lock);
    int index = sem_pool_index(sem);
    if (index >= 0)
        sem_pool[index].used = 0;
    spin_unlock(&sem_pool_lock);
}

static int sys_sem_valid(sys_sem_t sem)
{
    spin_lock(&sem_pool_lock);
    int index = sem_pool_index(sem);
    int valid = index >= 0 && sem_pool[index].used;
    spin_unlock(&sem_pool_lock);
    return valid;
}

net_err_t sys_sem_wait(sys_sem_t sem, int timeout_ms)
{
    if (sem == SYS_SEM_INVALID || !sys_sem_valid(sem))
        return NET_ERR_PARAM;
    if (timeout_ms < 0) {
        spin_lock(&sem->sem.lock);
        net_err_t error = NET_ERR_TMO;
        if (sem->sem.count > 0) {
            sem->sem.count--;
            error = NET_ERR_OK;
        }
        spin_unlock(&sem->sem.lock);
        return error;
    }
    if (timeout_ms == 0)
        return sem_wait(&sem->sem) == 0 ? NET_ERR_OK : NET_ERR_SYS;

    u64 deadline = r_mtime() + (u64)timeout_ms * MTIME_TICKS_PER_MS;
    return sem_timedwait(&sem->sem, deadline) == 0 ? NET_ERR_OK : NET_ERR_TMO;
}

void sys_sem_notify(sys_sem_t sem)
{
    if (sem != SYS_SEM_INVALID && sys_sem_valid(sem))
        sem_post(&sem->sem);
}

#else

#include <errno.h>
#include <pthread.h>
#include <time.h>

void *malloc(size_t size);
void free(void *ptr);

struct net_sys_sem {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    int count;
};

net_err_t net_sys_init(void)
{
    return NET_ERR_OK;
}

void sys_time_curr(net_time_t *time)
{
    if (time == 0)
        return;

    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        *time = 0;
        return;
    }
    *time = (net_time_t)now.tv_sec * 1000ULL +
            (net_time_t)now.tv_nsec / 1000000ULL;
}

sys_sem_t sys_sem_create(int initial_count)
{
    if (initial_count < 0)
        return SYS_SEM_INVALID;

    sys_sem_t sem = malloc(sizeof(*sem));
    if (sem == SYS_SEM_INVALID)
        return SYS_SEM_INVALID;
    if (pthread_mutex_init(&sem->mutex, 0) != 0) {
        free(sem);
        return SYS_SEM_INVALID;
    }

    pthread_condattr_t attributes;
    if (pthread_condattr_init(&attributes) != 0) {
        pthread_mutex_destroy(&sem->mutex);
        free(sem);
        return SYS_SEM_INVALID;
    }
    int result = pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC);
    if (result == 0)
        result = pthread_cond_init(&sem->condition, &attributes);
    pthread_condattr_destroy(&attributes);
    if (result != 0) {
        pthread_mutex_destroy(&sem->mutex);
        free(sem);
        return SYS_SEM_INVALID;
    }

    sem->count = initial_count;
    return sem;
}

void sys_sem_free(sys_sem_t sem)
{
    if (sem == SYS_SEM_INVALID)
        return;
    pthread_cond_destroy(&sem->condition);
    pthread_mutex_destroy(&sem->mutex);
    free(sem);
}

net_err_t sys_sem_wait(sys_sem_t sem, int timeout_ms)
{
    if (sem == SYS_SEM_INVALID)
        return NET_ERR_PARAM;
    if (pthread_mutex_lock(&sem->mutex) != 0)
        return NET_ERR_SYS;

    int result = 0;
    if (timeout_ms < 0) {
        if (sem->count == 0)
            result = ETIMEDOUT;
    } else if (timeout_ms == 0) {
        while (sem->count == 0 && result == 0)
            result = pthread_cond_wait(&sem->condition, &sem->mutex);
    } else {
        struct timespec deadline;
        result = clock_gettime(CLOCK_MONOTONIC, &deadline);
        if (result == 0) {
            deadline.tv_sec += timeout_ms / 1000;
            deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
            if (deadline.tv_nsec >= 1000000000L) {
                deadline.tv_sec++;
                deadline.tv_nsec -= 1000000000L;
            }
        }
        while (sem->count == 0 && result == 0)
            result = pthread_cond_timedwait(&sem->condition, &sem->mutex,
                                            &deadline);
    }

    net_err_t error = NET_ERR_OK;
    if (result == 0)
        sem->count--;
    else if (result == ETIMEDOUT)
        error = NET_ERR_TMO;
    else
        error = NET_ERR_SYS;
    if (pthread_mutex_unlock(&sem->mutex) != 0)
        error = NET_ERR_SYS;
    return error;
}

void sys_sem_notify(sys_sem_t sem)
{
    if (sem == SYS_SEM_INVALID || pthread_mutex_lock(&sem->mutex) != 0)
        return;
    if (sem->count < INT_MAX)
        sem->count++;
    pthread_cond_signal(&sem->condition);
    pthread_mutex_unlock(&sem->mutex);
}

#endif

int sys_time_goes(net_time_t *previous)
{
    if (previous == 0)
        return 0;

    net_time_t current;
    sys_time_curr(&current);
    net_time_t elapsed = current - *previous;
    *previous = current;
    return elapsed > INT_MAX ? INT_MAX : (int)elapsed;
}
