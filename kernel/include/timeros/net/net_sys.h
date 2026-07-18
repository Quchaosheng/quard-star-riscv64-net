#ifndef TOS_NET_SYS_H__
#define TOS_NET_SYS_H__

#include <timeros/net/net_err.h>
#include <timeros/types.h>

typedef u64 net_time_t;
typedef struct net_sys_sem *sys_sem_t;

#define SYS_SEM_INVALID ((sys_sem_t)0)

net_err_t net_sys_init(void);
void sys_time_curr(net_time_t *time);
int sys_time_goes(net_time_t *previous);
sys_sem_t sys_sem_create(int initial_count);
void sys_sem_free(sys_sem_t sem);
net_err_t sys_sem_wait(sys_sem_t sem, int timeout_ms);
void sys_sem_notify(sys_sem_t sem);

#endif
