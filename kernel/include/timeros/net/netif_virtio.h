#ifndef TOS_NET_NETIF_VIRTIO_H
#define TOS_NET_NETIF_VIRTIO_H

#include <timeros/net/netif.h>

net_err_t netif_virtio_open(netif_t *netif, void *data);
void netif_virtio_close(netif_t *netif);
net_err_t netif_virtio_xmit(netif_t *netif);
net_err_t netif_virtio_poll(netif_t *netif, u64 deadline);

extern const netif_ops_t netif_virtio_ops;

#endif
