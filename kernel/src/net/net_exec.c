#include <timeros/net/net_exec.h>

#include <timeros/net/fixq.h>
#include <timeros/net/net_cfg.h>
#include <timeros/net/net_sys.h>
#include <timeros/net/nlocker.h>

#define NET_EXEC_QUEUE_SIZE 8

typedef struct _net_exec_req_t {
    net_exec_proc_t proc;
    void *arg;
    sys_sem_t done;
    int completed;
    int abandoned;
    int started;
} net_exec_req_t;

static fixq_t request_queue;
static net_exec_req_t requests[NET_EXEC_QUEUE_SIZE];
static void *request_slots[NET_EXEC_QUEUE_SIZE];
static nlocker_t request_locker;
static int initialized;

net_err_t net_exec_init(void)
{
    if (initialized)
        return NET_ERR_EXIST;
    net_err_t err = fixq_init(&request_queue, request_slots,
                              NET_EXEC_QUEUE_SIZE, NLOCKER_THREAD);
    if (err < 0)
        return err;
    nlocker_init(&request_locker, NLOCKER_THREAD);
    for (int i = 0; i < NET_EXEC_QUEUE_SIZE; i++) {
        requests[i].proc = 0;
        requests[i].done = sys_sem_create(0);
        if (requests[i].done == SYS_SEM_INVALID)
            return NET_ERR_MEM;
    }
    initialized = 1;
    return NET_ERR_OK;
}

net_err_t net_exec_submit(net_exec_proc_t proc, void *arg, int timeout_ms)
{
    if (!initialized || proc == 0)
        return NET_ERR_PARAM;
    net_exec_req_t *request = 0;
    nlocker_lock(&request_locker);
    for (int i = 0; i < NET_EXEC_QUEUE_SIZE; i++) {
        if (requests[i].proc == 0) {
            request = &requests[i];
            request->proc = proc;
            request->arg = arg;
            request->completed = 0;
            request->abandoned = 0;
            request->started = 0;
            break;
        }
    }
    nlocker_unlock(&request_locker);
    if (request == 0)
        return NET_ERR_FULL;
    net_err_t err = fixq_send(&request_queue, request, timeout_ms);
    if (err < 0) {
        nlocker_lock(&request_locker);
        request->proc = 0;
        nlocker_unlock(&request_locker);
        return err;
    }
    err = sys_sem_wait(request->done, timeout_ms);
    nlocker_lock(&request_locker);
    if (err < 0 && request->started && !request->completed) {
        nlocker_unlock(&request_locker);
        err = sys_sem_wait(request->done, 0);
        nlocker_lock(&request_locker);
    }
    if (request->completed) {
        if (err < 0)
            (void)sys_sem_wait(request->done, -1);
        request->proc = 0;
        err = NET_ERR_OK;
    } else {
        request->abandoned = 1;
    }
    nlocker_unlock(&request_locker);
    return err;
}

net_err_t net_exec_run_once(void)
{
    if (!initialized)
        return NET_ERR_STATE;
    net_exec_req_t *request = fixq_recv(&request_queue, -1);
    if (request == 0)
        return NET_ERR_NONE;
    nlocker_lock(&request_locker);
    if (request->abandoned) {
        request->proc = 0;
        nlocker_unlock(&request_locker);
        return NET_ERR_OK;
    }
    request->started = 1;
    nlocker_unlock(&request_locker);
    request->proc(request->arg);
    nlocker_lock(&request_locker);
    int notify = !request->abandoned;
    if (!notify)
        request->proc = 0;
    else {
        request->completed = 1;
        sys_sem_notify(request->done);
    }
    nlocker_unlock(&request_locker);
    return NET_ERR_OK;
}

int net_exec_pending(void)
{
    return initialized ? fixq_count(&request_queue) : 0;
}
