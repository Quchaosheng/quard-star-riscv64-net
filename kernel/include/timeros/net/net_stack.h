#ifndef TOS_NET_STACK_H
#define TOS_NET_STACK_H

#include <timeros/net/netif.h>

net_err_t net_stack_init(void);
netif_t *net_stack_default(void);
net_err_t net_stack_process_input(netif_t *netif);
net_err_t net_stack_poll_once(netif_t *netif, u64 deadline);
void net_stack_worker(void *arg);

#endif
