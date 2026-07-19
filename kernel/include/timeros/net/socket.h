#ifndef TOS_NET_SOCKET_H
#define TOS_NET_SOCKET_H

#include <timeros/net/net_err.h>
#include <timeros/net/udp.h>

#define NET_SOCKET_UDP 2
#define NET_SOCKET_MAX 16

net_err_t net_socket_init(void);
int net_socket_open(int type);
net_err_t net_socket_bind(int handle, uint16_t port);
net_err_t net_socket_close(int handle);

#endif
