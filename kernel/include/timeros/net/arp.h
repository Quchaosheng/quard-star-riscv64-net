#ifndef TOS_NET_ARP_H
#define TOS_NET_ARP_H

#include <stdint.h>

#include <timeros/net/ether.h>
#include <timeros/net/ipaddr.h>
#include <timeros/net/netif.h>
#include <timeros/net/nlist.h>

#define ARP_HW_ETHER 0x0001
#define ARP_REQUEST 0x0001
#define ARP_REPLY 0x0002

typedef struct __attribute__((packed)) _arp_pkt_t {
    uint16_t htype;
    uint16_t ptype;
    uint8_t hlen;
    uint8_t plen;
    uint16_t opcode;
    uint8_t send_haddr[ETH_HWA_SIZE];
    uint8_t send_paddr[IPV4_ADDR_SIZE];
    uint8_t target_haddr[ETH_HWA_SIZE];
    uint8_t target_paddr[IPV4_ADDR_SIZE];
} arp_pkt_t;

typedef enum {
    NET_ARP_FREE = 0,
    NET_ARP_RESOLVED,
    NET_ARP_WAITING,
} arp_state_t;

typedef struct _arp_entry_t {
    nlist_node_t node;
    ipaddr_t paddr;
    uint8_t haddr[ETH_HWA_SIZE];
    arp_state_t state;
    netif_t *netif;
    nlist_t buf_list;
} arp_entry_t;

net_err_t arp_init(void);
net_err_t arp_make_request(netif_t *netif, const ipaddr_t *protocol_addr);
net_err_t arp_make_gratuitous(netif_t *netif);
net_err_t arp_in(netif_t *netif, pktbuf_t *buf);
net_err_t arp_make_reply(netif_t *netif, pktbuf_t *buf);
net_err_t arp_resolve(netif_t *netif, const ipaddr_t *ipaddr,
                      pktbuf_t *buf);
const uint8_t *arp_find(netif_t *netif, const ipaddr_t *ipaddr);
void arp_clear(netif_t *netif);
void arp_update_from_ipbuf(netif_t *netif, pktbuf_t *buf);

#endif
