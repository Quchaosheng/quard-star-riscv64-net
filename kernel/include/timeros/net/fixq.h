#ifndef TOS_NET_FIXQ_H__
#define TOS_NET_FIXQ_H__

#include <timeros/net/net_err.h>
#include <timeros/net/net_sys.h>
#include <timeros/net/nlocker.h>

typedef struct {
    int size;
    void **buf;
    int in;
    int out;
    int count;
    sys_sem_t send_sem;
    sys_sem_t recv_sem;
    nlocker_t locker;
} fixq_t;

net_err_t fixq_init(fixq_t *queue, void **buf, int size, nlocker_type_t type);
net_err_t fixq_send(fixq_t *queue, void *msg, int timeout);
void *fixq_recv(fixq_t *queue, int timeout);
void fixq_destroy(fixq_t *queue);
int fixq_count(fixq_t *queue);

#endif
