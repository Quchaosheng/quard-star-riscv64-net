#include <timeros/net/ether.h>

#include <timeros/net/arp.h>
#include <timeros/net/net_port.h>
#include <timeros/net/tools.h>

#define ETHER_HANDLER_MAX 4

struct ether_handler {
    uint16_t protocol;
    ether_input_handler_t handler;
};

static struct ether_handler handlers[ETHER_HANDLER_MAX];

static net_err_t ether_link_open(netif_t *netif)
{
    (void)netif;
    return NET_ERR_OK;
}

static void ether_link_close(netif_t *netif)
{
    arp_clear(netif);
}

static net_err_t ether_link_in(netif_t *netif, pktbuf_t *buf)
{
    return ether_in(netif, buf);
}

static net_err_t ether_link_out(netif_t *netif, ipaddr_t *dest,
                                pktbuf_t *buf)
{
    return arp_resolve(netif, dest, buf);
}

static const link_layer_t ether_link_layer = {
    .type = NETIF_TYPE_ETHER,
    .open = ether_link_open,
    .close = ether_link_close,
    .in = ether_link_in,
    .out = ether_link_out,
};

static struct ether_handler *ether_find_handler(uint16_t protocol)
{
    for (int i = 0; i < ETHER_HANDLER_MAX; i++) {
        if (handlers[i].handler != 0 && handlers[i].protocol == protocol)
            return &handlers[i];
    }
    return 0;
}

static int ether_is_broadcast(const uint8_t *addr)
{
    for (int i = 0; i < ETH_HWA_SIZE; i++) {
        if (addr[i] != 0xff)
            return 0;
    }
    return 1;
}

const uint8_t *ether_broadcast_addr(void)
{
    static const uint8_t broadcast[ETH_HWA_SIZE] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    };

    return broadcast;
}

net_err_t ether_register_handler(uint16_t protocol,
                                 ether_input_handler_t handler)
{
    if (handler == 0)
        return NET_ERR_PARAM;
    if (ether_find_handler(protocol) != 0)
        return NET_ERR_EXIST;
    for (int i = 0; i < ETHER_HANDLER_MAX; i++) {
        if (handlers[i].handler == 0) {
            handlers[i].protocol = protocol;
            handlers[i].handler = handler;
            return NET_ERR_OK;
        }
    }
    return NET_ERR_FULL;
}

net_err_t ether_raw_out(netif_t *netif, uint16_t protocol,
                        const uint8_t *dest, pktbuf_t *buf)
{
    if (buf == 0)
        return NET_ERR_PARAM;
    if (netif == 0 || dest == 0) {
        pktbuf_free(buf);
        return NET_ERR_PARAM;
    }
    if (netif->state != NETIF_ACTIVE || netif->hwaddr.len < ETH_HWA_SIZE) {
        pktbuf_free(buf);
        return NET_ERR_STATE;
    }

    int payload_size = pktbuf_total(buf);
    if (payload_size < 0 || payload_size > ETH_MTU) {
        pktbuf_free(buf);
        return NET_ERR_SIZE;
    }
    if (payload_size < ETH_DATA_MIN) {
        net_err_t err = pktbuf_resize(buf, ETH_DATA_MIN);
        if (err < 0) {
            pktbuf_free(buf);
            return err;
        }
        pktbuf_reset_acc(buf);
        err = pktbuf_seek(buf, payload_size);
        if (err < 0) {
            pktbuf_free(buf);
            return err;
        }
        err = pktbuf_fill(buf, 0, ETH_DATA_MIN - payload_size);
        if (err < 0) {
            pktbuf_free(buf);
            return err;
        }
    }

    net_err_t err = pktbuf_add_header(buf, sizeof(ether_hdr_t), 1);
    if (err < 0) {
        pktbuf_free(buf);
        return err;
    }
    ether_hdr_t *header = (ether_hdr_t *)pktbuf_data(buf);
    if (header == 0) {
        pktbuf_free(buf);
        return NET_ERR_SIZE;
    }
    plat_memcpy(header->dest, dest, ETH_HWA_SIZE);
    plat_memcpy(header->src, netif->hwaddr.addr, ETH_HWA_SIZE);
    header->protocol = x_htons(protocol);

    if (plat_memcmp(netif->hwaddr.addr, dest, ETH_HWA_SIZE) == 0) {
        err = netif_put_in(netif, buf, -1);
        if (err < 0)
            pktbuf_free(buf);
        return err;
    }
    if (netif->ops == 0 || netif->ops->xmit == 0) {
        pktbuf_free(buf);
        return NET_ERR_NOT_SUPPORT;
    }
    err = netif_put_out(netif, buf, -1);
    if (err < 0) {
        pktbuf_free(buf);
        return err;
    }
    return netif->ops->xmit(netif);
}

net_err_t ether_in(netif_t *netif, pktbuf_t *packet)
{
    if (netif == 0 || packet == 0)
        return NET_ERR_PARAM;
    int total_size = pktbuf_total(packet);
    if (total_size < ETH_FRAME_MIN ||
        total_size > ETH_FRAME_MAX)
        return NET_ERR_SIZE;
    if (pktbuf_set_cont(packet, sizeof(ether_hdr_t)) != NET_ERR_OK)
        return NET_ERR_SIZE;

    ether_hdr_t *header = (ether_hdr_t *)pktbuf_data(packet);
    if (header == 0)
        return NET_ERR_SIZE;
    if (netif->hwaddr.len >= ETH_HWA_SIZE &&
        plat_memcmp(header->dest, netif->hwaddr.addr, ETH_HWA_SIZE) != 0 &&
        !ether_is_broadcast(header->dest))
        return NET_ERR_UNREACH;
    uint16_t protocol = x_ntohs(header->protocol);
    struct ether_handler *entry = ether_find_handler(protocol);
    if (pktbuf_remove_header(packet, sizeof(ether_hdr_t)) != NET_ERR_OK)
        return NET_ERR_SIZE;
    pktbuf_reset_acc(packet);
    if (entry == 0)
        return NET_ERR_NOT_SUPPORT;
    return entry->handler(netif, packet);
}

net_err_t ether_init(void)
{
    plat_memset(handlers, 0, sizeof(handlers));
    return netif_register_layer(NETIF_TYPE_ETHER, &ether_link_layer);
}
