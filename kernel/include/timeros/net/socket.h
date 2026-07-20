#ifndef TOS_NET_SOCKET_H
#define TOS_NET_SOCKET_H

#include <timeros/net/net_err.h>
#include <timeros/net/tcp.h>
#include <timeros/net/udp.h>

#define NET_SOCKET_TCP NET_SOCKET_STREAM
#define NET_SOCKET_UDP 2
#define NET_SOCKET_MAX 16

typedef struct _net_socket_accept_t {
    int listener_handle;
    tcp_pcb_t *listener;
    tcp_pcb_t *child;
    ipaddr_t remote_ip;
    uint16_t remote_port;
    int acquired;
} net_socket_accept_t;

net_err_t net_socket_init(void);
int net_socket_open(int type);
net_err_t net_socket_bind(int handle, netif_t *netif,
                          const ipaddr_t *local, uint16_t port);
net_err_t net_socket_listen(int handle, int backlog);
net_err_t net_socket_accept_prepare(int handle,
                                    net_socket_accept_t *accept);
int net_socket_accept_commit(net_socket_accept_t *accept);
void net_socket_accept_abort(net_socket_accept_t *accept);
net_err_t net_socket_close(int handle);
net_err_t net_socket_connect_start(int handle, netif_t *netif,
                                   const ipaddr_t *dest, uint16_t dest_port);
net_err_t net_socket_wait_connect(int handle, int timeout_ms);
net_err_t net_socket_send(int handle, const uint8_t *data, int size);
int net_socket_recv(int handle, uint8_t *data, int size, int timeout_ms);
net_err_t net_socket_wait_close(int handle, int timeout_ms);
net_err_t net_socket_sendto(int handle, netif_t *netif,
                            const ipaddr_t *dest, uint16_t dest_port,
                            const uint8_t *data, int size);
int net_socket_recvfrom(int handle, uint8_t *data, int size, ipaddr_t *src,
                        uint16_t *src_port, int timeout_ms);

#endif
