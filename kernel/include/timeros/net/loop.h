#ifndef TOS_NET_LOOP_H
#define TOS_NET_LOOP_H

#include <timeros/net/netif.h>

net_err_t loop_init(void);
netif_t *loop_get_netif(void);

#endif
