#ifndef TOS_NET_ETHER_H
#define TOS_NET_ETHER_H

#include <stdint.h>

#include <timeros/net/net_err.h>
#include <timeros/net/netif.h>

#define ETH_HWA_SIZE 6
#define ETH_MTU 1500
#define ETH_DATA_MIN 46
#define ETH_FRAME_MIN (ETH_HWA_SIZE + ETH_HWA_SIZE + 2 + ETH_DATA_MIN)
#define ETH_FRAME_MAX (ETH_HWA_SIZE + ETH_HWA_SIZE + 2 + ETH_MTU)

typedef struct __attribute__((packed)) _ether_hdr_t {
    uint8_t dest[ETH_HWA_SIZE];
    uint8_t src[ETH_HWA_SIZE];
    uint16_t protocol;
} ether_hdr_t;

typedef net_err_t (*ether_input_handler_t)(netif_t *netif, pktbuf_t *buf);

const uint8_t *ether_broadcast_addr(void);
net_err_t ether_register_handler(uint16_t protocol,
                                 ether_input_handler_t handler);
net_err_t ether_raw_out(netif_t *netif, uint16_t protocol,
                        const uint8_t *dest, pktbuf_t *buf);
net_err_t ether_init(void);
net_err_t ether_in(netif_t *netif, pktbuf_t *packet);

#endif
