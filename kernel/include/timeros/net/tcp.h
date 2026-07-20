#ifndef TOS_NET_TCP_H
#define TOS_NET_TCP_H

#include <stdint.h>

#include <timeros/net/ipaddr.h>
#include <timeros/net/net_err.h>
#include <timeros/net/net_sys.h>
#include <timeros/net/netif.h>
#include <timeros/net/nlocker.h>
#include <timeros/net/pktbuf.h>
#include <timeros/net/timer.h>

#define NET_SOCKET_STREAM 1
#define TCP_HEADER_SIZE 20
#define TCP_PCB_MAX 8
#define TCP_RECV_MAX 2048
#define TCP_MSS 512
#define TCP_RETRANS_MS 500
#define TCP_RETRY_MAX 5
#define TCP_TIME_WAIT_MS 1000

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10
#define TCP_FLAG_URG 0x20

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

typedef struct _tcp_pcb_t {
    int opened;
    netif_t *netif;
    ipaddr_t local_ip;
    ipaddr_t remote_ip;
    uint16_t local_port;
    uint16_t remote_port;
    tcp_state_t state;
    uint32_t iss;
    uint32_t snd_una;
    uint32_t snd_nxt;
    uint32_t rcv_nxt;
    int peer_fin_seen;
    uint16_t window;
    pktbuf_t *outstanding;
    uint32_t outstanding_end;
    int retry_count;
    net_timer_t retrans_timer;
    net_timer_t time_wait_timer;
    uint8_t recv_storage[TCP_RECV_MAX];
    int recv_head;
    int recv_count;
    nlocker_t recv_locker;
    nlocker_t state_locker;
    int connect_waiters;
    int recv_waiters;
    int close_waiters;
    int release_pending;
    int socket_attached;
    sys_sem_t connect_done;
    sys_sem_t recv_done;
    sys_sem_t close_done;
    net_err_t error;
    int terminal_notified;
} tcp_pcb_t;

net_err_t tcp_init(void);
net_err_t tcp_open(tcp_pcb_t **result);
/* Socket-owned PCBs remain pooled until the matching detach succeeds. */
net_err_t tcp_socket_open(tcp_pcb_t **result);
net_err_t tcp_socket_detach(tcp_pcb_t *pcb);
net_err_t tcp_connect_start(tcp_pcb_t *pcb, netif_t *netif,
                            const ipaddr_t *remote, uint16_t remote_port);
net_err_t tcp_send_start(tcp_pcb_t *pcb, const uint8_t *data, int size);
int tcp_recv_bytes(tcp_pcb_t *pcb, uint8_t *data, int size,
                   int timeout_ms);
/* Acquire a waiter before releasing an owning socket lock. */
/* Each _acquired call requires a successful matching acquire first. */
net_err_t tcp_recv_acquire(tcp_pcb_t *pcb);
int tcp_recv_bytes_acquired(tcp_pcb_t *pcb, uint8_t *data, int size,
                            int timeout_ms);
net_err_t tcp_retransmit_due(tcp_pcb_t *pcb);
/* Call from the network worker; repeat after tcp_wait_close to finalize. */
net_err_t tcp_close(tcp_pcb_t *pcb);
/* Completion waits track PCB lifetime; callers must not wait on sem fields. */
net_err_t tcp_wait_connect(tcp_pcb_t *pcb, int timeout_ms);
net_err_t tcp_wait_connect_acquire(tcp_pcb_t *pcb);
net_err_t tcp_wait_connect_acquired(tcp_pcb_t *pcb, int timeout_ms);
net_err_t tcp_wait_close(tcp_pcb_t *pcb, int timeout_ms);
net_err_t tcp_wait_close_acquire(tcp_pcb_t *pcb);
net_err_t tcp_wait_close_acquired(tcp_pcb_t *pcb, int timeout_ms);
net_err_t tcp_in(netif_t *netif, const ipaddr_t *src,
                 const ipaddr_t *dest, pktbuf_t *buf);
net_err_t tcp_header_check(const tcp_hdr_t *header, int size);
/* Returns 0 when buf, src, or dest is null; callers validate send inputs. */
uint16_t tcp_checksum(pktbuf_t *buf, const ipaddr_t *src,
                      const ipaddr_t *dest, uint16_t length);
int tcp_sequence_in_window(uint32_t sequence, uint32_t start,
                           uint16_t window);
net_err_t tcp_state_accept_ack(tcp_state_t state, uint32_t ack,
                               uint32_t expected_ack);

#endif
