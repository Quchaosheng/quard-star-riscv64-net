#include <timeros/net/fixq.h>

net_err_t fixq_init(fixq_t *queue, void **buf, int size, nlocker_type_t type)
{
    if (queue == 0 || buf == 0 || size <= 0)
        return NET_ERR_PARAM;
    queue->size = size;
    queue->buf = buf;
    queue->in = queue->out = queue->count = 0;
    nlocker_init(&queue->locker, type);
    return NET_ERR_OK;
}

net_err_t fixq_send(fixq_t *queue, void *msg, int timeout)
{
    (void)timeout;
    if (queue == 0 || msg == 0)
        return NET_ERR_PARAM;
    nlocker_lock(&queue->locker);
    if (queue->count == queue->size) {
        nlocker_unlock(&queue->locker);
        return NET_ERR_FULL;
    }
    queue->buf[queue->in] = msg;
    queue->in = (queue->in + 1) % queue->size;
    queue->count++;
    nlocker_unlock(&queue->locker);
    return NET_ERR_OK;
}

void *fixq_recv(fixq_t *queue, int timeout)
{
    (void)timeout;
    if (queue == 0)
        return 0;
    nlocker_lock(&queue->locker);
    if (queue->count == 0) {
        nlocker_unlock(&queue->locker);
        return 0;
    }
    void *msg = queue->buf[queue->out];
    queue->out = (queue->out + 1) % queue->size;
    queue->count--;
    nlocker_unlock(&queue->locker);
    return msg;
}

void fixq_destroy(fixq_t *queue)
{
    if (queue != 0)
        nlocker_destroy(&queue->locker);
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
