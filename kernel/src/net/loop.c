#include <timeros/net/loop.h>

#include <timeros/net/ether.h>

static netif_t *loop_netif;

static net_err_t loop_open(netif_t *netif, void *data)
{
    (void)data;
    netif->type = NETIF_TYPE_LOOP;
    netif->mtu = ETH_MTU;
    return NET_ERR_OK;
}

static void loop_close(netif_t *netif)
{
    if (loop_netif == netif)
        loop_netif = 0;
}

static net_err_t loop_xmit(netif_t *netif)
{
    pktbuf_t *buf = netif_get_out(netif, -1);

    if (buf == 0)
        return NET_ERR_NONE;
    net_err_t err = netif_put_in(netif, buf, -1);
    if (err < 0)
        pktbuf_free(buf);
    return err;
}

static const netif_ops_t loop_ops = {
    .open = loop_open,
    .close = loop_close,
    .xmit = loop_xmit,
};

net_err_t loop_init(void)
{
    ipaddr_t ip;
    ipaddr_t mask;

    if (loop_netif != 0)
        return NET_ERR_EXIST;
    netif_t *netif = netif_open("loop", &loop_ops, 0);
    if (netif == 0)
        return NET_ERR_IO;
    loop_netif = netif;

    net_err_t err = ipaddr_from_str(&ip, "127.0.0.1");
    if (err >= 0)
        err = ipaddr_from_str(&mask, "255.0.0.0");
    if (err >= 0)
        err = netif_set_addr(netif, &ip, &mask, 0);
    if (err >= 0)
        err = netif_set_active(netif);
    if (err < 0) {
        (void)netif_close(netif);
        return err;
    }
    return NET_ERR_OK;
}

netif_t *loop_get_netif(void)
{
    return loop_netif;
}
