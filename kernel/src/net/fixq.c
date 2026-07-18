#include <timeros/net/fixq.h>

net_err_t fixq_init(fixq_t *queue, void **buf, int size, nlocker_type_t type)
{
    if (queue == 0 || buf == 0 || size <= 0)
        return NET_ERR_PARAM;
    queue->size = size;
    queue->buf = buf;
    queue->in = queue->out = queue->count = 0;
    nlocker_init(&queue->locker, type);
    queue->send_sem = queue->recv_sem = SYS_SEM_INVALID;
    queue->send_sem = sys_sem_create(size);
    if (queue->send_sem == SYS_SEM_INVALID) {
        nlocker_destroy(&queue->locker);
        return NET_ERR_MEM;
    }
    queue->recv_sem = sys_sem_create(0);
    if (queue->recv_sem == SYS_SEM_INVALID) {
        sys_sem_free(queue->send_sem);
        queue->send_sem = SYS_SEM_INVALID;
        nlocker_destroy(&queue->locker);
        return NET_ERR_MEM;
    }
    return NET_ERR_OK;
}

net_err_t fixq_send(fixq_t *queue, void *msg, int timeout)
{
    if (queue == 0 || msg == 0)
        return NET_ERR_PARAM;

    if (timeout < 0) {
        nlocker_lock(&queue->locker);
        int full = queue->count == queue->size;
        nlocker_unlock(&queue->locker);
        if (full)
            return NET_ERR_FULL;
    }

    net_err_t error = sys_sem_wait(queue->send_sem, timeout);
    if (error < 0)
        return timeout < 0 && error == NET_ERR_TMO ? NET_ERR_FULL : error;

    nlocker_lock(&queue->locker);
    queue->buf[queue->in] = msg;
    queue->in = (queue->in + 1) % queue->size;
    queue->count++;
    nlocker_unlock(&queue->locker);
    sys_sem_notify(queue->recv_sem);
    return NET_ERR_OK;
}

void *fixq_recv(fixq_t *queue, int timeout)
{
    if (queue == 0)
        return 0;

    if (timeout < 0) {
        nlocker_lock(&queue->locker);
        int empty = queue->count == 0;
        nlocker_unlock(&queue->locker);
        if (empty)
            return 0;
    }

    if (sys_sem_wait(queue->recv_sem, timeout) < 0)
        return 0;

    nlocker_lock(&queue->locker);
    void *msg = queue->buf[queue->out];
    queue->out = (queue->out + 1) % queue->size;
    queue->count--;
    nlocker_unlock(&queue->locker);
    sys_sem_notify(queue->send_sem);
    return msg;
}

void fixq_destroy(fixq_t *queue)
{
    if (queue != 0) {
        sys_sem_free(queue->send_sem);
        sys_sem_free(queue->recv_sem);
        nlocker_destroy(&queue->locker);
    }
}

int fixq_count(fixq_t *queue)
{
    if (queue == 0)
        return -1;
    nlocker_lock(&queue->locker);
    int count = queue->count;
    nlocker_unlock(&queue->locker);
    return count;
}
