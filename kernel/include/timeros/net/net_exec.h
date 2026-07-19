#ifndef TOS_NET_EXEC_H
#define TOS_NET_EXEC_H

#include <timeros/net/net_err.h>

typedef void (*net_exec_proc_t)(void *arg);

net_err_t net_exec_init(void);
net_err_t net_exec_submit(net_exec_proc_t proc, void *arg, int timeout_ms);
net_err_t net_exec_run_once(void);
int net_exec_pending(void);

#endif
