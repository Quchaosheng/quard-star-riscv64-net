#ifndef TOS_NET_NLOCKER_H__
#define TOS_NET_NLOCKER_H__

#include <timeros/net/net_port.h>

typedef enum {
    NLOCKER_NONE,
    NLOCKER_THREAD,
} nlocker_type_t;

typedef struct {
    nlocker_type_t type;
    struct spinlock lock;
} nlocker_t;

void nlocker_init(nlocker_t *locker, nlocker_type_t type);
void nlocker_destroy(nlocker_t *locker);
void nlocker_lock(nlocker_t *locker);
void nlocker_unlock(nlocker_t *locker);

#endif
