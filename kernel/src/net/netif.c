#include <timeros/net/netif.h>

#include <timeros/net/mblock.h>
#include <timeros/net/net_port.h>

static netif_t netif_buffer[NETIF_DEV_CNT];
static mblock_t netif_mblock;
static nlist_t netif_list;
static netif_t *netif_default;
static const link_layer_t *link_layers[NETIF_TYPE_SIZE];

static void netif_name_copy(char *dest, const char *src)
{
    int i = 0;

    if (src != 0) {
        for (; i < NETIF_NAME_SIZE - 1 && src[i] != '\0'; i++)
            dest[i] = src[i];
    }
    dest[i] = '\0';
    for (i++; i < NETIF_NAME_SIZE; i++)
        dest[i] = '\0';
}

static void netif_drain_queue(fixq_t *queue)
{
    pktbuf_t *buf;

    while ((buf = (pktbuf_t *)fixq_recv(queue, -1)) != 0)
        pktbuf_free(buf);
}

net_err_t netif_init(void)
{
    nlist_init(&netif_list);
    netif_default = 0;
    for (int i = 0; i < NETIF_TYPE_SIZE; i++)
        link_layers[i] = 0;
    return mblock_init(&netif_mblock, netif_buffer, sizeof(netif_t),
                       NETIF_DEV_CNT, NLOCKER_NONE);
}

netif_t *netif_open(const char *dev_name, const netif_ops_t *ops,
                    void *ops_data)
{
    if (dev_name == 0 || ops == 0 || ops->open == 0)
        return 0;

    netif_t *netif = (netif_t *)mblock_alloc(&netif_mblock, -1);
    if (netif == 0)
        return 0;
    plat_memset(netif, 0, sizeof(*netif));
    netif_name_copy(netif->name, dev_name);
    ipaddr_set_any(&netif->ipaddr);
    ipaddr_set_any(&netif->netmask);
    ipaddr_set_any(&netif->gateway);
    netif->state = NETIF_CLOSED;
    netif->type = NETIF_TYPE_NONE;
    netif->ops = ops;
    netif->ops_data = ops_data;
    nlist_node_init(&netif->node);

    if (fixq_init(&netif->in_q, netif->in_q_buf, NETIF_INQ_SIZE,
                  NLOCKER_THREAD) != NET_ERR_OK ||
        fixq_init(&netif->out_q, netif->out_q_buf, NETIF_OUTQ_SIZE,
                  NLOCKER_THREAD) != NET_ERR_OK) {
        fixq_destroy(&netif->in_q);
        mblock_free(&netif_mblock, netif);
        return 0;
    }

    if (ops->open(netif, ops_data) < 0 || netif->type == NETIF_TYPE_NONE) {
        if (ops->close != 0)
            ops->close(netif);
        fixq_destroy(&netif->in_q);
        fixq_destroy(&netif->out_q);
        mblock_free(&netif_mblock, netif);
        return 0;
    }
    netif->link_layer = netif->type < NETIF_TYPE_SIZE ?
                        link_layers[netif->type] : 0;
    if (netif->type != NETIF_TYPE_LOOP && netif->link_layer == 0) {
        if (ops->close != 0)
            ops->close(netif);
        fixq_destroy(&netif->in_q);
        fixq_destroy(&netif->out_q);
        mblock_free(&netif_mblock, netif);
        return 0;
    }
    netif->state = NETIF_OPENED;
    nlist_insert_last(&netif_list, &netif->node);
    return netif;
}

net_err_t netif_set_addr(netif_t *netif, const ipaddr_t *ip,
                         const ipaddr_t *netmask, const ipaddr_t *gateway)
{
    if (netif == 0)
        return NET_ERR_PARAM;
    ipaddr_copy(&netif->ipaddr, ip != 0 ? ip : ipaddr_get_any());
    ipaddr_copy(&netif->netmask, netmask != 0 ? netmask : ipaddr_get_any());
    ipaddr_copy(&netif->gateway, gateway != 0 ? gateway : ipaddr_get_any());
    return NET_ERR_OK;
}

net_err_t netif_set_hwaddr(netif_t *netif, const uint8_t *hwaddr, int len)
{
    if (netif == 0 || hwaddr == 0 || len <= 0)
        return NET_ERR_PARAM;
    if (len > NETIF_HWADDR_SIZE)
        return NET_ERR_SIZE;
    plat_memcpy(netif->hwaddr.addr, hwaddr, (size_t)len);
    netif->hwaddr.len = (uint8_t)len;
    return NET_ERR_OK;
}

net_err_t netif_set_active(netif_t *netif)
{
    if (netif == 0)
        return NET_ERR_PARAM;
    if (netif->state != NETIF_OPENED)
        return NET_ERR_STATE;
    if (netif->link_layer != 0 && netif->link_layer->open != 0) {
        net_err_t err = netif->link_layer->open(netif);
        if (err < 0)
            return err;
    }
    netif->state = NETIF_ACTIVE;
    return NET_ERR_OK;
}

net_err_t netif_set_deactive(netif_t *netif)
{
    if (netif == 0)
        return NET_ERR_PARAM;
    if (netif->state != NETIF_ACTIVE)
        return NET_ERR_STATE;
    if (netif->link_layer != 0 && netif->link_layer->close != 0)
        netif->link_layer->close(netif);
    netif_drain_queue(&netif->in_q);
    netif_drain_queue(&netif->out_q);
    netif->state = NETIF_OPENED;
    return NET_ERR_OK;
}

void netif_set_default(netif_t *netif)
{
    netif_default = netif;
}

netif_t *netif_get_default(void)
{
    return netif_default;
}

net_err_t netif_close(netif_t *netif)
{
    if (netif == 0)
        return NET_ERR_PARAM;
    if (netif->state != NETIF_OPENED)
        return NET_ERR_STATE;
    if (netif->ops != 0 && netif->ops->close != 0)
        netif->ops->close(netif);
    netif_drain_queue(&netif->in_q);
    netif_drain_queue(&netif->out_q);
    fixq_destroy(&netif->in_q);
    fixq_destroy(&netif->out_q);
    nlist_remove(&netif_list, &netif->node);
    if (netif_default == netif)
        netif_default = 0;
    netif->state = NETIF_CLOSED;
    mblock_free(&netif_mblock, netif);
    return NET_ERR_OK;
}

net_err_t netif_register_layer(int type, const link_layer_t *layer)
{
    if (type < 0 || type >= NETIF_TYPE_SIZE || layer == 0)
        return NET_ERR_PARAM;
    if (link_layers[type] != 0)
        return NET_ERR_EXIST;
    link_layers[type] = layer;
    return NET_ERR_OK;
}

net_err_t netif_put_in(netif_t *netif, pktbuf_t *buf, int timeout)
{
    if (netif == 0 || buf == 0)
        return NET_ERR_PARAM;
    return fixq_send(&netif->in_q, buf, timeout);
}

net_err_t netif_put_out(netif_t *netif, pktbuf_t *buf, int timeout)
{
    if (netif == 0 || buf == 0)
        return NET_ERR_PARAM;
    return fixq_send(&netif->out_q, buf, timeout);
}

pktbuf_t *netif_get_in(netif_t *netif, int timeout)
{
    if (netif == 0)
        return 0;
    pktbuf_t *buf = (pktbuf_t *)fixq_recv(&netif->in_q, timeout);
    if (buf != 0)
        pktbuf_reset_acc(buf);
    return buf;
}

pktbuf_t *netif_get_out(netif_t *netif, int timeout)
{
    if (netif == 0)
        return 0;
    pktbuf_t *buf = (pktbuf_t *)fixq_recv(&netif->out_q, timeout);
    if (buf != 0)
        pktbuf_reset_acc(buf);
    return buf;
}

net_err_t netif_out(netif_t *netif, ipaddr_t *dest, pktbuf_t *buf)
{
    if (netif == 0 || buf == 0)
        return NET_ERR_PARAM;
    if (netif->state != NETIF_ACTIVE)
        return NET_ERR_STATE;
    if (netif->link_layer != 0 && netif->link_layer->out != 0)
        return netif->link_layer->out(netif, dest, buf);
    if (netif->ops == 0 || netif->ops->xmit == 0)
        return NET_ERR_NOT_SUPPORT;
    net_err_t err = netif_put_out(netif, buf, -1);
    if (err < 0)
        return err;
    return netif->ops->xmit(netif);
}
