#include <timeros/net/ipv4.h>

#include <timeros/net/arp.h>
#include <timeros/net/ether.h>
#include <timeros/net/net_port.h>
#include <timeros/net/protocol.h>
#include <timeros/net/tools.h>

#define IPV4_HANDLER_MAX 256

static ipv4_input_handler_t handlers[IPV4_HANDLER_MAX];
static uint16_t next_packet_id;

static ipv4_input_handler_t ipv4_find_handler(uint8_t protocol)
{
    return handlers[protocol];
}

net_err_t ipv4_register_handler(uint8_t protocol,
                                ipv4_input_handler_t handler)
{
    if (handler == 0)
        return NET_ERR_PARAM;
    ipv4_input_handler_t *slot = &handlers[protocol];
    if (*slot != 0)
        return NET_ERR_EXIST;
    *slot = handler;
    return NET_ERR_OK;
}

net_err_t ipv4_init(void)
{
    plat_memset(handlers, 0, sizeof(handlers));
    next_packet_id = 0;
    return ether_register_handler(NET_PROTOCOL_IPV4, ipv4_in);
}

net_err_t ipv4_in(netif_t *netif, pktbuf_t *buf)
{
    if (netif == 0 || buf == 0)
        return NET_ERR_PARAM;
    int total_size = pktbuf_total(buf);
    if (total_size < IPV4_HEADER_MIN)
        return NET_ERR_SIZE;
    if (pktbuf_set_cont(buf, IPV4_HEADER_MIN) != NET_ERR_OK)
        return NET_ERR_SIZE;

    ipv4_hdr_t *header = (ipv4_hdr_t *)pktbuf_data(buf);
    if (header == 0)
        return NET_ERR_SIZE;
    uint8_t version = (uint8_t)(header->version_ihl >> 4);
    int header_size = (header->version_ihl & 0x0f) * 4;
    if (version != NET_VERSION_IPV4 || header_size < IPV4_HEADER_MIN ||
        header_size > IPV4_HEADER_MAX || header_size > total_size)
        return NET_ERR_FORMAT;
    if (header_size > IPV4_HEADER_MIN &&
        pktbuf_set_cont(buf, header_size) != NET_ERR_OK)
        return NET_ERR_SIZE;
    header = (ipv4_hdr_t *)pktbuf_data(buf);
    if (header == 0)
        return NET_ERR_SIZE;

    uint16_t packet_size = x_ntohs(header->total_len);
    if (packet_size < (uint16_t)header_size || packet_size > total_size)
        return NET_ERR_SIZE;
    if (pktbuf_resize(buf, packet_size) != NET_ERR_OK)
        return NET_ERR_SIZE;
    pktbuf_reset_acc(buf);
    if (pktbuf_checksum16(buf, header_size, 0, 1) != 0)
        return NET_ERR_CHKSUM;
    if ((x_ntohs(header->frag_off) & 0x3fffU) != 0)
        return NET_ERR_NOT_SUPPORT;

    ipaddr_t src;
    ipaddr_t dest;
    ipaddr_from_buf(&src, header->src_ip);
    ipaddr_from_buf(&dest, header->dest_ip);
    if (!ipaddr_is_match(&dest, &netif->ipaddr, &netif->netmask))
        return NET_ERR_UNREACH;

    ipv4_input_handler_t handler = ipv4_find_handler(header->protocol);
    if (handler == 0)
        return NET_ERR_NOT_SUPPORT;
    if (pktbuf_remove_header(buf, header_size) != NET_ERR_OK)
        return NET_ERR_SIZE;
    pktbuf_reset_acc(buf);
    return handler(netif, &src, &dest, buf);
}

net_err_t ipv4_out(netif_t *netif, const ipaddr_t *dest,
                   uint8_t protocol, pktbuf_t *buf)
{
    if (buf == 0)
        return NET_ERR_PARAM;
    if (netif == 0 || dest == 0) {
        pktbuf_free(buf);
        return NET_ERR_PARAM;
    }
    if (netif->state != NETIF_ACTIVE) {
        pktbuf_free(buf);
        return NET_ERR_STATE;
    }
    int payload_size = pktbuf_total(buf);
    if (payload_size <= 0 || payload_size > netif->mtu - IPV4_HEADER_MIN) {
        pktbuf_free(buf);
        return NET_ERR_SIZE;
    }
    net_err_t err = pktbuf_add_header(buf, IPV4_HEADER_MIN, 1);
    if (err < 0) {
        pktbuf_free(buf);
        return err;
    }
    ipv4_hdr_t *header = (ipv4_hdr_t *)pktbuf_data(buf);
    if (header == 0) {
        pktbuf_free(buf);
        return NET_ERR_SIZE;
    }
    header->version_ihl = (uint8_t)((NET_VERSION_IPV4 << 4) | 5);
    header->dscp = 0;
    header->total_len = x_htons((uint16_t)(payload_size + IPV4_HEADER_MIN));
    header->id = x_htons(next_packet_id++);
    header->frag_off = x_htons(0x4000);
    header->ttl = NET_IP_DEF_TTL;
    header->protocol = protocol;
    header->hdr_checksum = 0;
    ipaddr_to_buf(&netif->ipaddr, header->src_ip);
    ipaddr_to_buf(dest, header->dest_ip);
    header->hdr_checksum =
        x_htons(checksum16(0, header, IPV4_HEADER_MIN, 0, 1));

    err = arp_resolve(netif, dest, buf);
    return err;
}
