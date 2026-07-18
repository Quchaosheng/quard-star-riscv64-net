#include <timeros/net/icmpv4.h>

#include <timeros/net/ipv4.h>
#include <timeros/net/net_port.h>
#include <timeros/net/protocol.h>
#include <timeros/net/tools.h>

static net_err_t icmpv4_echo_reply(netif_t *netif, const ipaddr_t *src,
                                   pktbuf_t *buf)
{
    if (pktbuf_set_cont(buf, sizeof(icmpv4_hdr_t)) != NET_ERR_OK)
        return NET_ERR_SIZE;
    icmpv4_hdr_t *header = (icmpv4_hdr_t *)pktbuf_data(buf);
    if (header == 0)
        return NET_ERR_SIZE;
    header->type = ICMPV4_ECHO_REPLY;
    header->code = ICMPV4_ECHO_CODE;
    header->checksum = 0;
    pktbuf_reset_acc(buf);
    header->checksum =
        x_htons(pktbuf_checksum16(buf, pktbuf_total(buf), 0, 1));
    return ipv4_out(netif, src, NET_PROTOCOL_ICMPV4, buf);
}

static icmpv4_stats_t icmpv4_stats;

net_err_t icmpv4_init(void)
{
    plat_memset(&icmpv4_stats, 0, sizeof(icmpv4_stats));
    return ipv4_register_handler(NET_PROTOCOL_ICMPV4, icmpv4_in);
}

void icmpv4_get_stats(icmpv4_stats_t *stats)
{
    if (stats != 0)
        *stats = icmpv4_stats;
}

net_err_t icmpv4_in(netif_t *netif, const ipaddr_t *src,
                    const ipaddr_t *dest, pktbuf_t *buf)
{
    (void)dest;
    if (netif == 0 || src == 0 || buf == 0)
        return NET_ERR_PARAM;
    if (pktbuf_total(buf) < (int)sizeof(icmpv4_hdr_t))
        return NET_ERR_SIZE;
    if (pktbuf_set_cont(buf, sizeof(icmpv4_hdr_t)) != NET_ERR_OK)
        return NET_ERR_SIZE;
    pktbuf_reset_acc(buf);
    if (pktbuf_checksum16(buf, pktbuf_total(buf), 0, 1) != 0)
        return NET_ERR_CHKSUM;
    icmpv4_hdr_t *header = (icmpv4_hdr_t *)pktbuf_data(buf);
    if (header == 0)
        return NET_ERR_SIZE;
    if (header->code != ICMPV4_ECHO_CODE) {
        return NET_ERR_NOT_SUPPORT;
    }
    if (header->type == ICMPV4_ECHO_REPLY) {
        icmpv4_stats.echo_replies++;
        icmpv4_stats.last_reply_identifier = x_ntohs(header->identifier);
        icmpv4_stats.last_reply_sequence = x_ntohs(header->sequence);
        pktbuf_free(buf);
        return NET_ERR_OK;
    }
    if (header->type != ICMPV4_ECHO_REQUEST) {
        return NET_ERR_NOT_SUPPORT;
    }
    icmpv4_stats.echo_requests++;
    net_err_t err = icmpv4_echo_reply(netif, src, buf);
    return err;
}

net_err_t icmpv4_out_echo(netif_t *netif, const ipaddr_t *dest,
                          uint16_t identifier, uint16_t sequence,
                          const uint8_t *payload, int payload_len)
{
    if (netif == 0 || dest == 0 || payload_len < 0 ||
        (payload_len != 0 && payload == 0))
        return NET_ERR_PARAM;
    if (payload_len > netif->mtu - IPV4_HEADER_MIN -
        (int)sizeof(icmpv4_hdr_t))
        return NET_ERR_SIZE;
    pktbuf_t *buf = pktbuf_alloc(sizeof(icmpv4_hdr_t) + payload_len);
    if (buf == 0)
        return NET_ERR_MEM;
    icmpv4_hdr_t *header = (icmpv4_hdr_t *)pktbuf_data(buf);
    header->type = ICMPV4_ECHO_REQUEST;
    header->code = ICMPV4_ECHO_CODE;
    header->checksum = 0;
    header->identifier = x_htons(identifier);
    header->sequence = x_htons(sequence);
    if (payload_len != 0) {
        pktbuf_reset_acc(buf);
        if (pktbuf_seek(buf, sizeof(icmpv4_hdr_t)) != NET_ERR_OK ||
            pktbuf_write(buf, payload, payload_len) != NET_ERR_OK) {
            pktbuf_free(buf);
            return NET_ERR_SIZE;
        }
    }
    pktbuf_reset_acc(buf);
    header = (icmpv4_hdr_t *)pktbuf_data(buf);
    header->checksum =
        x_htons(pktbuf_checksum16(buf, pktbuf_total(buf), 0, 1));
    return ipv4_out(netif, dest, NET_PROTOCOL_ICMPV4, buf);
}
