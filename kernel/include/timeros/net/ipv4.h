#ifndef TOS_NET_IPV4_H
#define TOS_NET_IPV4_H

#include <stdint.h>

#include <timeros/net/net_err.h>
#include <timeros/net/netif.h>

#define NET_VERSION_IPV4 4
#define NET_IP_DEF_TTL 64
#define IPV4_HEADER_MIN 20
#define IPV4_HEADER_MAX 60

typedef struct __attribute__((packed)) _ipv4_hdr_t {
    uint8_t version_ihl;
    uint8_t dscp;
    uint16_t total_len;
    uint16_t id;
    uint16_t frag_off;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t hdr_checksum;
    uint8_t src_ip[IPV4_ADDR_SIZE];
    uint8_t dest_ip[IPV4_ADDR_SIZE];
} ipv4_hdr_t;

typedef net_err_t (*ipv4_input_handler_t)(netif_t *netif,
                                           const ipaddr_t *src,
                                           const ipaddr_t *dest,
                                           pktbuf_t *buf);

net_err_t ipv4_init(void);
net_err_t ipv4_register_handler(uint8_t protocol,
                                ipv4_input_handler_t handler);
net_err_t ipv4_in(netif_t *netif, pktbuf_t *buf);
net_err_t ipv4_out(netif_t *netif, const ipaddr_t *dest,
                   uint8_t protocol, pktbuf_t *buf);

#endif
