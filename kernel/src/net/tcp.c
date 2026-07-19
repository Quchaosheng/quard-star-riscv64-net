#include <timeros/net/tcp.h>

#include <timeros/net/protocol.h>
#include <timeros/net/tools.h>

typedef struct __attribute__((packed)) _tcp_pseudo_t {
    uint8_t src[IPV4_ADDR_SIZE];
    uint8_t dest[IPV4_ADDR_SIZE];
    uint8_t zero;
    uint8_t protocol;
    uint16_t length;
} tcp_pseudo_t;

net_err_t tcp_header_check(const tcp_hdr_t *header, int size)
{
    if (header == 0 || size < TCP_HEADER_SIZE)
        return NET_ERR_SIZE;

    int header_size = (header->data_offset >> 4) * 4;
    if (header_size < TCP_HEADER_SIZE || header_size > size)
        return NET_ERR_SIZE;
    if ((header->data_offset & 0x0fU) != 0)
        return NET_ERR_FORMAT;
    return NET_ERR_OK;
}

uint16_t tcp_checksum(pktbuf_t *buf, const ipaddr_t *src,
                      const ipaddr_t *dest, uint16_t length)
{
    if (buf == 0 || src == 0 || dest == 0)
        return 0;

    tcp_pseudo_t pseudo;

    ipaddr_to_buf(src, pseudo.src);
    ipaddr_to_buf(dest, pseudo.dest);
    pseudo.zero = 0;
    pseudo.protocol = NET_PROTOCOL_TCP;
    pseudo.length = x_htons(length);
    uint32_t sum = checksum16(0, &pseudo, sizeof(pseudo), 0, 0);
    pktbuf_reset_acc(buf);
    return pktbuf_checksum16(buf, length, sum, 1);
}

int tcp_sequence_in_window(uint32_t sequence, uint32_t start,
                           uint16_t window)
{
    return window != 0 && (uint32_t)(sequence - start) < window;
}

net_err_t tcp_state_accept_ack(tcp_state_t state, uint32_t ack,
                               uint32_t expected_ack)
{
    if (state != TCP_STATE_SYN_SENT || ack != expected_ack + 1U)
        return NET_ERR_STATE;
    return NET_ERR_OK;
}
