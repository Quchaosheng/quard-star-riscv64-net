#include <timeros/net/netif_virtio.h>

#include <timeros/net/net_port.h>
#include <timeros/virtio_net.h>

net_err_t netif_virtio_open(netif_t *netif, void *data)
{
    (void)data;
    if (netif == 0)
        return NET_ERR_PARAM;

    u8 mac[6];
    if (virtio_net_get_mac(mac) < 0)
        return NET_ERR_IO;
    net_err_t err = netif_set_hwaddr(netif, mac, (int)sizeof(mac));
    if (err < 0)
        return err;
    netif->type = NETIF_TYPE_ETHER;
    netif->mtu = ETHERNET_MAX_FRAME - ETHERNET_HEADER_SIZE;
    return NET_ERR_OK;
}

void netif_virtio_close(netif_t *netif)
{
    (void)netif;
}

net_err_t netif_virtio_xmit(netif_t *netif)
{
    if (netif == 0)
        return NET_ERR_PARAM;

    net_err_t result = NET_ERR_OK;
    for (;;) {
        pktbuf_t *buf = netif_get_out(netif, -1);
        if (buf == 0)
            break;

        int size = pktbuf_total(buf);
        if (size < 0 || size > ETHERNET_MAX_FRAME) {
            pktbuf_free(buf);
            result = NET_ERR_SIZE;
            continue;
        }

        u8 frame[ETHERNET_MAX_FRAME];
        plat_memset(frame, 0, sizeof(frame));
        pktbuf_reset_acc(buf);
        if (size != 0 && pktbuf_read(buf, frame, size) != NET_ERR_OK) {
            pktbuf_free(buf);
            result = NET_ERR_SIZE;
            continue;
        }
        u32 frame_size = (u32)size;
        if (frame_size < ETHERNET_MIN_FRAME)
            frame_size = ETHERNET_MIN_FRAME;
        if (virtio_net_send(frame, frame_size) < 0) {
            pktbuf_free(buf);
            return NET_ERR_IO;
        }
        pktbuf_free(buf);
    }
    return result;
}

net_err_t netif_virtio_poll(netif_t *netif, u64 deadline)
{
    if (netif == 0)
        return NET_ERR_PARAM;

    u8 frame[ETHERNET_MAX_FRAME];
    u32 length = 0;
    if (virtio_net_receive(frame, sizeof(frame), &length, deadline) < 0)
        return NET_ERR_TMO;
    if (length < ETHERNET_MIN_FRAME || length > ETHERNET_MAX_FRAME)
        return NET_ERR_SIZE;

    pktbuf_t *buf = pktbuf_alloc((int)length);
    if (buf == 0)
        return NET_ERR_MEM;
    if (pktbuf_write(buf, frame, (int)length) != NET_ERR_OK) {
        pktbuf_free(buf);
        return NET_ERR_SIZE;
    }
    net_err_t err = netif_put_in(netif, buf, -1);
    if (err < 0) {
        pktbuf_free(buf);
        return err;
    }
    return NET_ERR_OK;
}

const netif_ops_t netif_virtio_ops = {
    .open = netif_virtio_open,
    .close = netif_virtio_close,
    .xmit = netif_virtio_xmit,
};
