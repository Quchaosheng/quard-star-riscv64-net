#ifndef TOS_NET_ICMPV4_H
#define TOS_NET_ICMPV4_H

#include <stdint.h>

#include <timeros/net/net_err.h>
#include <timeros/net/netif.h>

#define ICMPV4_ECHO_REPLY 0
#define ICMPV4_ECHO_REQUEST 8
#define ICMPV4_ECHO_CODE 0

typedef struct __attribute__((packed)) _icmpv4_hdr_t {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t identifier;
    uint16_t sequence;
} icmpv4_hdr_t;

typedef struct _icmpv4_stats_t {
    uint32_t echo_requests;
    uint32_t echo_replies;
    uint16_t last_reply_identifier;
    uint16_t last_reply_sequence;
} icmpv4_stats_t;

net_err_t icmpv4_init(void);
void icmpv4_get_stats(icmpv4_stats_t *stats);
net_err_t icmpv4_in(netif_t *netif, const ipaddr_t *src, const ipaddr_t *dest,
                    pktbuf_t *buf);
net_err_t icmpv4_out_echo(netif_t *netif, const ipaddr_t *dest,
                          uint16_t identifier, uint16_t sequence,
                          const uint8_t *payload, int payload_len);

#endif
