#ifndef TOS_NET_UDP_H
#define TOS_NET_UDP_H

#include <stdint.h>

#include <timeros/net/net_err.h>

#define UDP_HEADER_SIZE 8
#define UDP_PCB_MAX 16

typedef struct __attribute__((packed)) _udp_hdr_t {
    uint16_t src_port;
    uint16_t dest_port;
    uint16_t total_len;
    uint16_t checksum;
} udp_hdr_t;

typedef struct _udp_pcb_t {
    uint16_t local_port;
    int open;
} udp_pcb_t;

net_err_t udp_init(void);
net_err_t udp_open(udp_pcb_t *pcb);
net_err_t udp_bind(udp_pcb_t *pcb, uint16_t port);
net_err_t udp_close(udp_pcb_t *pcb);
net_err_t udp_header_check(const udp_hdr_t *header, int size);

#endif
