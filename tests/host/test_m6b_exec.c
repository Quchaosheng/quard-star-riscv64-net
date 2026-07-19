#include <assert.h>
#include <pthread.h>

#include <timeros/net/net_exec.h>
#include <timeros/net/net_sys.h>

static int callback_count;
static int callback_active;
static int callback_overlap;

static void execute_callback(void *arg)
{
    (void)arg;
    callback_active++;
    if (callback_active > 1)
        callback_overlap = 1;
    callback_count++;
    callback_active--;
}

static void *submit_thread(void *arg)
{
    (void)arg;
    assert(net_exec_submit(execute_callback, 0, 20) == NET_ERR_OK);
    return 0;
}

int main(void)
{
    pthread_t first;
    pthread_t second;

    assert(net_sys_init() == NET_ERR_OK);
    assert(net_exec_init() == NET_ERR_OK);
    assert(pthread_create(&first, 0, submit_thread, 0) == 0);
    assert(pthread_create(&second, 0, submit_thread, 0) == 0);
    assert(pthread_join(first, 0) == 0);
    assert(pthread_join(second, 0) == 0);
    assert(net_exec_run_once() == NET_ERR_OK);
    assert(net_exec_run_once() == NET_ERR_OK);
    assert(callback_count == 2);
    assert(callback_overlap == 0);
    return 0;
}
