#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <pthread.h>
#include <time.h>

#include <timeros/net/net_sys.h>

static void sleep_ms(long milliseconds)
{
    struct timespec delay = {
        .tv_sec = milliseconds / 1000,
        .tv_nsec = (milliseconds % 1000) * 1000000L,
    };
    assert(nanosleep(&delay, 0) == 0);
}

static void test_monotonic_time(void)
{
    net_time_t start;
    sys_time_curr(&start);
    sleep_ms(10);
    assert(sys_time_goes(&start) >= 5);

    net_time_t current;
    sys_time_curr(&current);
    assert(current >= start);
}

static void test_timed_wait(void)
{
    sys_sem_t sem = sys_sem_create(0);
    assert(sem != SYS_SEM_INVALID);

    net_time_t start;
    sys_time_curr(&start);
    assert(sys_sem_wait(sem, 20) == NET_ERR_TMO);
    assert(sys_time_goes(&start) >= 15);

    sys_sem_free(sem);
}

struct waiter_args {
    sys_sem_t sem;
    net_err_t result;
};

static void *wait_forever(void *arg)
{
    struct waiter_args *args = arg;
    args->result = sys_sem_wait(args->sem, 0);
    return 0;
}

static void test_wait_and_notify(void)
{
    sys_sem_t sem = sys_sem_create(0);
    assert(sem != SYS_SEM_INVALID);

    struct waiter_args args = { .sem = sem, .result = NET_ERR_SYS };
    pthread_t waiter;
    assert(pthread_create(&waiter, 0, wait_forever, &args) == 0);
    sleep_ms(10);
    sys_sem_notify(sem);
    assert(pthread_join(waiter, 0) == 0);
    assert(args.result == NET_ERR_OK);

    sys_sem_notify(sem);
    assert(sys_sem_wait(sem, 20) == NET_ERR_OK);
    sys_sem_free(sem);
}

static void test_try_wait_consumes_available_token(void)
{
    sys_sem_t sem = sys_sem_create(1);
    assert(sem != SYS_SEM_INVALID);
    assert(sys_sem_wait(sem, -1) == NET_ERR_OK);
    assert(sys_sem_wait(sem, -1) == NET_ERR_TMO);
    sys_sem_free(sem);
}

static void test_try_wait_returns_immediately_when_empty(void)
{
    sys_sem_t sem = sys_sem_create(0);
    assert(sem != SYS_SEM_INVALID);

    net_time_t start;
    sys_time_curr(&start);
    assert(sys_sem_wait(sem, -1) == NET_ERR_TMO);
    assert(sys_time_goes(&start) < 100);
    sys_sem_free(sem);
}

static void test_invalid_arguments(void)
{
    assert(sys_sem_create(-1) == SYS_SEM_INVALID);
    assert(sys_sem_wait(SYS_SEM_INVALID, 1) == NET_ERR_PARAM);

    assert(sys_time_goes(0) == 0);
    sys_time_curr(0);
    sys_sem_notify(SYS_SEM_INVALID);
    sys_sem_free(SYS_SEM_INVALID);
}

int main(void)
{
    assert(net_sys_init() == NET_ERR_OK);
    test_monotonic_time();
    test_timed_wait();
    test_wait_and_notify();
    test_try_wait_consumes_available_token();
    test_try_wait_returns_immediately_when_empty();
    test_invalid_arguments();
    return 0;
}
