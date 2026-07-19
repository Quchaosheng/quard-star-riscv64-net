#ifndef TOS_NET_TCP_H
#define TOS_NET_TCP_H

#include <stdint.h>

#include <timeros/net/ipaddr.h>
#include <timeros/net/net_err.h>
#include <timeros/net/pktbuf.h>

#define NET_SOCKET_STREAM 1
#define TCP_HEADER_SIZE 20
#define TCP_PCB_MAX 8
#define TCP_RECV_MAX 2048
#define TCP_MSS 512

typedef struct __attribute__((packed)) _tcp_hdr_t {
    uint16_t src_port;
    uint16_t dest_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t data_offset;
    uint8_t flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} tcp_hdr_t;

typedef enum _tcp_state_t {
    TCP_STATE_CLOSED,
    TCP_STATE_SYN_SENT,
    TCP_STATE_ESTABLISHED,
    TCP_STATE_FIN_WAIT_1,
    TCP_STATE_FIN_WAIT_2,
    TCP_STATE_TIME_WAIT,
} tcp_state_t;

net_err_t tcp_header_check(const tcp_hdr_t *header, int size);
uint16_t tcp_checksum(pktbuf_t *buf, const ipaddr_t *src,
                      const ipaddr_t *dest, uint16_t length);
int tcp_sequence_in_window(uint32_t sequence, uint32_t start,
                           uint16_t window);
net_err_t tcp_state_accept_ack(tcp_state_t state, uint32_t ack,
                               uint32_t expected_ack);

#endif
