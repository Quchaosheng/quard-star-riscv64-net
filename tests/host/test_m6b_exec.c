#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>

#include <timeros/net/net_exec.h>
#include <timeros/net/net_sys.h>

static int callback_count;
static int callback_active;
static int callback_overlap;
static int submit_done;
static int slow_started;
static int slow_release;

static void execute_callback(void *arg)
{
    (void)arg;
    callback_active++;
    if (callback_active > 1)
        callback_overlap = 1;
    callback_count++;
    callback_active--;
}

static void slow_callback(void *arg)
{
    (void)arg;
    __atomic_store_n(&slow_started, 1, __ATOMIC_RELEASE);
    while (!__atomic_load_n(&slow_release, __ATOMIC_ACQUIRE))
        sched_yield();
}

static void *slow_submit_thread(void *arg)
{
    (void)arg;
    assert(net_exec_submit(slow_callback, 0, 10) == NET_ERR_OK);
    return 0;
}

static void *submit_thread(void *arg)
{
    (void)arg;
    assert(net_exec_submit(execute_callback, 0, 20) == NET_ERR_OK);
    __atomic_add_fetch(&submit_done, 1, __ATOMIC_RELEASE);
    return 0;
}

static void *worker_thread(void *arg)
{
    (void)arg;
    while (callback_count < 2) {
        if (net_exec_run_once() == NET_ERR_NONE)
            sched_yield();
    }
    return 0;
}

int main(void)
{
    pthread_t first;
    pthread_t second;
    pthread_t worker;

    assert(net_sys_init() == NET_ERR_OK);
    assert(net_exec_init() == NET_ERR_OK);
    assert(pthread_create(&first, 0, submit_thread, 0) == 0);
    assert(pthread_create(&second, 0, submit_thread, 0) == 0);
    while (net_exec_pending() < 2)
        sched_yield();
    assert(__atomic_load_n(&submit_done, __ATOMIC_ACQUIRE) == 0);
    assert(pthread_create(&worker, 0, worker_thread, 0) == 0);
    assert(pthread_join(first, 0) == 0);
    assert(pthread_join(second, 0) == 0);
    assert(pthread_join(worker, 0) == 0);
    assert(callback_count == 2);
    assert(callback_overlap == 0);

    pthread_t slow_submit;
    pthread_t slow_worker;
    callback_count = 0;
    assert(pthread_create(&slow_submit, 0, slow_submit_thread, 0) == 0);
    while (net_exec_pending() == 0)
        sched_yield();
    assert(pthread_create(&slow_worker, 0, worker_thread, 0) == 0);
    while (!__atomic_load_n(&slow_started, __ATOMIC_ACQUIRE))
        sched_yield();
    struct timespec delay = { .tv_nsec = 30000000L };
    assert(nanosleep(&delay, 0) == 0);
    __atomic_store_n(&slow_release, 1, __ATOMIC_RELEASE);
    callback_count = 2;
    assert(pthread_join(slow_submit, 0) == 0);
    assert(pthread_join(slow_worker, 0) == 0);
    return 0;
}
