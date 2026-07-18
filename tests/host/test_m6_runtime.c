#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <time.h>

#include <timeros/net/fixq.h>
#include <timeros/net/mblock.h>
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

static void test_notify_saturates_count(void)
{
    sys_sem_t sem = sys_sem_create(INT_MAX);
    assert(sem != SYS_SEM_INVALID);
    sys_sem_notify(sem);
    assert(sys_sem_wait(sem, -1) == NET_ERR_OK);
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

static void test_fixq_timeout_and_ownership(void)
{
    fixq_t queue;
    void *items[1];
    int first;
    int second;
    assert(fixq_init(&queue, items, 1, NLOCKER_THREAD) == NET_ERR_OK);

    assert(fixq_send(&queue, &first, -1) == NET_ERR_OK);
    assert(fixq_send(&queue, &second, -1) == NET_ERR_FULL);

    net_time_t start;
    sys_time_curr(&start);
    assert(fixq_send(&queue, &second, 20) == NET_ERR_TMO);
    assert(sys_time_goes(&start) >= 15);
    assert(fixq_recv(&queue, -1) == &first);
    assert(fixq_recv(&queue, -1) == 0);

    sys_time_curr(&start);
    assert(fixq_recv(&queue, 20) == 0);
    assert(sys_time_goes(&start) >= 15);
    fixq_destroy(&queue);
}

struct send_waiter_args {
    fixq_t *queue;
    void *message;
    net_err_t result;
};

static void *send_forever(void *arg)
{
    struct send_waiter_args *args = arg;
    args->result = fixq_send(args->queue, args->message, 0);
    return 0;
}

static void test_fixq_permanent_send_wait(void)
{
    fixq_t queue;
    void *items[1];
    int first;
    int second;
    assert(fixq_init(&queue, items, 1, NLOCKER_THREAD) == NET_ERR_OK);
    assert(fixq_send(&queue, &first, -1) == NET_ERR_OK);

    struct send_waiter_args args = {
        .queue = &queue,
        .message = &second,
        .result = NET_ERR_SYS,
    };
    pthread_t waiter;
    assert(pthread_create(&waiter, 0, send_forever, &args) == 0);
    sleep_ms(10);
    assert(fixq_count(&queue) == 1);
    assert(fixq_recv(&queue, -1) == &first);
    assert(pthread_join(waiter, 0) == 0);
    assert(args.result == NET_ERR_OK);
    assert(fixq_recv(&queue, -1) == &second);
    assert(fixq_recv(&queue, -1) == 0);
    fixq_destroy(&queue);
}

struct recv_waiter_args {
    fixq_t *queue;
    void *result;
};

static void *recv_forever(void *arg)
{
    struct recv_waiter_args *args = arg;
    args->result = fixq_recv(args->queue, 0);
    return 0;
}

static void test_fixq_permanent_recv_wait(void)
{
    fixq_t queue;
    void *items[1];
    int message;
    assert(fixq_init(&queue, items, 1, NLOCKER_THREAD) == NET_ERR_OK);

    struct recv_waiter_args args = { .queue = &queue, .result = 0 };
    pthread_t waiter;
    assert(pthread_create(&waiter, 0, recv_forever, &args) == 0);
    sleep_ms(10);
    assert(fixq_count(&queue) == 0);
    assert(fixq_send(&queue, &message, -1) == NET_ERR_OK);
    assert(pthread_join(waiter, 0) == 0);
    assert(args.result == &message);
    assert(fixq_recv(&queue, -1) == 0);
    fixq_destroy(&queue);
}

struct alloc_waiter_args {
    mblock_t *blocks;
    void *result;
};

static void *alloc_forever(void *arg)
{
    struct alloc_waiter_args *args = arg;
    args->result = mblock_alloc(args->blocks, 0);
    return 0;
}

static void test_mblock_thread_waits(void)
{
    mblock_t blocks;
    nlist_node_t storage[1];
    assert(mblock_init(&blocks, storage, sizeof(storage[0]), 1,
                       NLOCKER_THREAD) == NET_ERR_OK);
    assert(blocks.alloc_sem != SYS_SEM_INVALID);

    void *only = mblock_alloc(&blocks, -1);
    assert(only == &storage[0]);
    assert(mblock_alloc(&blocks, -1) == 0);

    net_time_t start;
    sys_time_curr(&start);
    assert(mblock_alloc(&blocks, 20) == 0);
    assert(sys_time_goes(&start) >= 15);

    struct alloc_waiter_args args = { .blocks = &blocks, .result = 0 };
    pthread_t waiter;
    assert(pthread_create(&waiter, 0, alloc_forever, &args) == 0);
    sleep_ms(10);
    assert(mblock_free_cnt(&blocks) == 0);
    mblock_free(&blocks, only);
    assert(pthread_join(waiter, 0) == 0);
    assert(args.result == only);
    assert(mblock_alloc(&blocks, -1) == 0);
    mblock_free(&blocks, only);
    mblock_destroy(&blocks);
}

static void test_mblock_none_never_waits(void)
{
    mblock_t blocks;
    nlist_node_t storage[1];
    assert(mblock_init(&blocks, storage, sizeof(storage[0]), 1,
                       NLOCKER_NONE) == NET_ERR_OK);
    assert(blocks.alloc_sem == SYS_SEM_INVALID);
    assert(mblock_alloc(&blocks, -1) == &storage[0]);
    assert(mblock_alloc(&blocks, -1) == 0);

    net_time_t start;
    sys_time_curr(&start);
    assert(mblock_alloc(&blocks, 0) == 0);
    assert(sys_time_goes(&start) < 100);
    assert(mblock_free_cnt(&blocks) == 0);

    sys_time_curr(&start);
    assert(mblock_alloc(&blocks, 200) == 0);
    assert(sys_time_goes(&start) < 100);
    mblock_free(&blocks, &storage[0]);
    mblock_destroy(&blocks);
}

int main(void)
{
    assert(net_sys_init() == NET_ERR_OK);
    test_monotonic_time();
    test_timed_wait();
    test_wait_and_notify();
    test_try_wait_consumes_available_token();
    test_try_wait_returns_immediately_when_empty();
    test_notify_saturates_count();
    test_invalid_arguments();
    test_fixq_timeout_and_ownership();
    test_fixq_permanent_send_wait();
    test_fixq_permanent_recv_wait();
    test_mblock_thread_waits();
    test_mblock_none_never_waits();
    return 0;
}
