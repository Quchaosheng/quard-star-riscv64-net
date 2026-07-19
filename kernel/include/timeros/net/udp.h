#ifndef TOS_NET_UDP_H
#define TOS_NET_UDP_H

#include <stdint.h>

#include <timeros/net/net_err.h>
#include <timeros/net/ipaddr.h>
#include <timeros/net/netif.h>
#include <timeros/net/fixq.h>

#define UDP_HEADER_SIZE 8
#define UDP_PCB_MAX 16
#define UDP_RECV_MAX 8

typedef struct __attribute__((packed)) _udp_hdr_t {
    uint16_t src_port;
    uint16_t dest_port;
    uint16_t total_len;
    uint16_t checksum;
} udp_hdr_t;

typedef struct _udp_pcb_t {
    uint16_t local_port;
    int open;
    fixq_t recv_queue;
    void *recv_slots[UDP_RECV_MAX];
} udp_pcb_t;

net_err_t udp_init(void);
net_err_t udp_open(udp_pcb_t *pcb);
net_err_t udp_bind(udp_pcb_t *pcb, uint16_t port);
net_err_t udp_close(udp_pcb_t *pcb);
net_err_t udp_header_check(const udp_hdr_t *header, int size);
net_err_t udp_in(netif_t *netif, const ipaddr_t *src,
                 const ipaddr_t *dest, pktbuf_t *buf);
net_err_t udp_sendto(udp_pcb_t *pcb, netif_t *netif, const ipaddr_t *dest,
                     uint16_t dest_port, const uint8_t *data, int size);
int udp_recvfrom(udp_pcb_t *pcb, uint8_t *data, int size, ipaddr_t *src,
                 uint16_t *src_port, int timeout_ms);

#endif
