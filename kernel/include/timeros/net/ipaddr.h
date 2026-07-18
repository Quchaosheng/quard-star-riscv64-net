#ifndef TOS_NET_IPADDR_H
#define TOS_NET_IPADDR_H

#include <stdint.h>

#include <timeros/net/net_err.h>

#define IPV4_ADDR_BROADCAST 0xffffffffU
#define IPV4_ADDR_SIZE 4

typedef struct _ipaddr_t {
    uint32_t q_addr;
} ipaddr_t;

void ipaddr_set_any(ipaddr_t *ip);
net_err_t ipaddr_from_str(ipaddr_t *dest, const char *str);
ipaddr_t *ipaddr_get_any(void);
void ipaddr_copy(ipaddr_t *dest, const ipaddr_t *src);
int ipaddr_is_equal(const ipaddr_t *left, const ipaddr_t *right);
void ipaddr_to_buf(const ipaddr_t *src, uint8_t *ip_buf);
void ipaddr_from_buf(ipaddr_t *dest, const uint8_t *ip_buf);
int ipaddr_is_local_broadcast(const ipaddr_t *ip);
int ipaddr_is_direct_broadcast(const ipaddr_t *ip,
                               const ipaddr_t *netmask);
int ipaddr_is_any(const ipaddr_t *ip);
ipaddr_t ipaddr_get_net(const ipaddr_t *ip, const ipaddr_t *netmask);
int ipaddr_is_match(const ipaddr_t *dest, const ipaddr_t *src,
                    const ipaddr_t *netmask);
void ipaddr_set_all_1(ipaddr_t *ip);
int ipaddr_1_cnt(const ipaddr_t *ip);
void ipaddr_set_loop(ipaddr_t *ip);

#endif
