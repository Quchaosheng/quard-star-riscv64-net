#ifndef TOS_NET_DNS_H
#define TOS_NET_DNS_H

#include <timeros/net/ipaddr.h>
#include <timeros/types.h>

typedef struct {
    u16 id;
    u8 bytes[512];
    int length;
} dns_query_t;

int dns_query_encode(dns_query_t *query, u16 id, const char *name);
int dns_response_parse(const u8 *packet, int length, u16 id,
                       const char *name, ipaddr_t *address);

#endif
