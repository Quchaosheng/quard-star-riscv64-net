#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <string.h>

#include <timeros/net/ether.h>
#include <timeros/net/ipv4.h>
#include <timeros/net/net_sys.h>
#include <timeros/net/netif.h>
#include <timeros/net/pktbuf.h>
#include <timeros/net/tcp.h>
#include <timeros/net/timer.h>
#include <timeros/net/tools.h>

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10

static int output_count;
static uint32_t last_output_seq;
static uint8_t last_output_flags;
static int last_output_checksum_ok;

static net_err_t test_netif_open(netif_t *netif, void *data)
{
    (void)data;
    netif->type = NETIF_TYPE_LOOP;
    netif->mtu = 1500;
    return NET_ERR_OK;
}

static net_err_t test_netif_xmit(netif_t *netif)
{
    pktbuf_t *buf = netif_get_out(netif, -1);

    assert(buf != 0);
    assert(pktbuf_set_cont(buf, IPV4_HEADER_MIN + TCP_HEADER_SIZE) ==
           NET_ERR_OK);
    ipv4_hdr_t *ip_header = (ipv4_hdr_t *)pktbuf_data(buf);
    int ip_header_size = (ip_header->version_ihl & 0x0f) * 4;
    int tcp_size = x_ntohs(ip_header->total_len) - ip_header_size;
    ipaddr_t src;
    ipaddr_t dest;
    ipaddr_from_buf(&src, ip_header->src_ip);
    ipaddr_from_buf(&dest, ip_header->dest_ip);
    assert(pktbuf_remove_header(buf, ip_header_size) == NET_ERR_OK);
    tcp_hdr_t *tcp_header = (tcp_hdr_t *)pktbuf_data(buf);
    last_output_seq = x_ntohl(tcp_header->seq);
    last_output_flags = tcp_header->flags;
    last_output_checksum_ok = tcp_checksum(buf, &src, &dest,
                                           (uint16_t)tcp_size) == 0;
    output_count++;
    pktbuf_free(buf);
    return NET_ERR_OK;
}

static const netif_ops_t test_netif_ops = {
    .open = test_netif_open,
    .xmit = test_netif_xmit,
};

static netif_t *make_netif(void)
{
    ipaddr_t ip;
    ipaddr_t mask;
    netif_t *netif = netif_open("tcp0", &test_netif_ops, 0);

    assert(netif != 0);
    assert(ipaddr_from_str(&ip, "192.0.2.10") == NET_ERR_OK);
    assert(ipaddr_from_str(&mask, "255.255.255.0") == NET_ERR_OK);
    assert(netif_set_addr(netif, &ip, &mask, 0) == NET_ERR_OK);
    assert(netif_set_active(netif) == NET_ERR_OK);
    netif_set_default(netif);
    return netif;
}

static pktbuf_t *make_segment(const ipaddr_t *src, const ipaddr_t *dest,
                              uint16_t src_port, uint16_t dest_port,
                              uint32_t seq, uint32_t ack, uint8_t flags,
                              const uint8_t *data, int size)
{
    int total = TCP_HEADER_SIZE + size;
    pktbuf_t *buf = pktbuf_alloc(total);

    assert(buf != 0);
    tcp_hdr_t *header = (tcp_hdr_t *)pktbuf_data(buf);
    memset(header, 0, sizeof(*header));
    header->src_port = x_htons(src_port);
    header->dest_port = x_htons(dest_port);
    header->seq = x_htonl(seq);
    header->ack = x_htonl(ack);
    header->data_offset = 5U << 4;
    header->flags = flags;
    header->window = x_htons(4096);
    if (size > 0) {
        pktbuf_reset_acc(buf);
        assert(pktbuf_seek(buf, TCP_HEADER_SIZE) == NET_ERR_OK);
        assert(pktbuf_write(buf, data, size) == NET_ERR_OK);
    }
    header = (tcp_hdr_t *)pktbuf_data(buf);
    header->checksum = x_htons(tcp_checksum(buf, src, dest, (uint16_t)total));
    return buf;
}

static void input_ok(netif_t *netif, const ipaddr_t *src,
                     const ipaddr_t *dest, pktbuf_t *buf)
{
    assert(tcp_in(netif, src, dest, buf) == NET_ERR_OK);
}

static void establish(tcp_pcb_t *pcb, netif_t *netif,
                      const ipaddr_t *remote, uint16_t port)
{
    assert(tcp_open(pcb) == NET_ERR_OK);
    assert(tcp_connect_start(pcb, netif, remote, port) == NET_ERR_OK);
    assert(tcp_wait_connect(pcb, -1) == NET_ERR_NONE);
    assert(pcb->state == TCP_STATE_SYN_SENT);
    assert(pcb->local_port != 0);
    assert(pcb->outstanding != 0);
    static const uint8_t invalid_data = 1;
    pktbuf_t *invalid = make_segment(remote, &netif->ipaddr, port,
                                     pcb->local_port, 7000,
                                     pcb->iss + 1U,
                                     TCP_FLAG_SYN | TCP_FLAG_ACK,
                                     &invalid_data, 1);
    assert(tcp_in(netif, remote, &netif->ipaddr, invalid) == NET_ERR_FORMAT);
    pktbuf_free(invalid);
    assert(pcb->state == TCP_STATE_SYN_SENT);
    pktbuf_t *syn_ack = make_segment(remote, &netif->ipaddr, port,
                                     pcb->local_port, 7000,
                                     pcb->iss + 1U,
                                     TCP_FLAG_SYN | TCP_FLAG_ACK, 0, 0);
    input_ok(netif, remote, &netif->ipaddr, syn_ack);
    assert(pcb->state == TCP_STATE_ESTABLISHED);
    assert(pcb->snd_una == pcb->iss + 1U);
    assert(pcb->rcv_nxt == 7001);
    assert(pcb->outstanding == 0);
    assert(tcp_wait_connect(pcb, -1) == NET_ERR_OK);
}

static void test_open_close_restores_slots(void)
{
    tcp_pcb_t pcbs[TCP_PCB_MAX + 1];

    memset(pcbs, 0, sizeof(pcbs));
    for (int i = 0; i < TCP_PCB_MAX; i++)
        assert(tcp_open(&pcbs[i]) == NET_ERR_OK);
    assert(tcp_open(&pcbs[TCP_PCB_MAX]) == NET_ERR_MEM);
    for (int i = 0; i < TCP_PCB_MAX; i++)
        assert(tcp_close(&pcbs[i]) == NET_ERR_OK);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    for (int i = 0; i < TCP_PCB_MAX; i++)
        assert(tcp_open(&pcbs[i]) == NET_ERR_OK);
    for (int i = 0; i < TCP_PCB_MAX; i++)
        assert(tcp_close(&pcbs[i]) == NET_ERR_OK);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
}

static void test_connect_allocation_rollback(netif_t *netif,
                                             const ipaddr_t *remote)
{
    tcp_pcb_t pcb = { 0 };
    pktbuf_t *held[PKTBUF_BUF_CNT];
    int count = 0;

    assert(tcp_open(&pcb) == NET_ERR_OK);
    while (count < PKTBUF_BUF_CNT &&
           (held[count] = pktbuf_alloc(0)) != 0)
        count++;
    assert(count != 0);
    assert(tcp_connect_start(&pcb, netif, remote, 4799) == NET_ERR_MEM);
    assert(pcb.state == TCP_STATE_CLOSED);
    assert(pcb.netif == 0);
    assert(pcb.local_port == 0 && pcb.remote_port == 0);
    assert(ipaddr_is_any(&pcb.local_ip));
    assert(ipaddr_is_any(&pcb.remote_ip));
    assert(pcb.outstanding == 0);
    for (int i = 0; i < count; i++)
        pktbuf_free(held[i]);
    assert(tcp_connect_start(&pcb, netif, remote, 4799) == NET_ERR_OK);
    assert(tcp_close(&pcb) == NET_ERR_OK);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
}

static void test_connect_and_retransmit(netif_t *netif,
                                        const ipaddr_t *remote)
{
    tcp_pcb_t pcb = { 0 };
    int started = output_count;

    assert(tcp_open(&pcb) == NET_ERR_OK);
    assert(tcp_connect_start(&pcb, netif, remote, 4800) == NET_ERR_OK);
    assert(pcb.state == TCP_STATE_SYN_SENT);
    assert(pcb.retry_count == 0);
    assert(output_count == started + 1);
    uint32_t syn_seq = last_output_seq;
    assert(last_output_flags == TCP_FLAG_SYN);
    assert(last_output_checksum_ok);
    assert(net_timer_first_tmo() == 500);
    assert(net_timer_check_tmo(500) == NET_ERR_OK);
    assert(pcb.retry_count == 1);
    assert(output_count == started + 2);
    assert(last_output_seq == syn_seq);
    assert(last_output_flags == TCP_FLAG_SYN);
    assert(last_output_checksum_ok);
    for (int i = 1; i < 4; i++)
        assert(tcp_retransmit_due(&pcb) == NET_ERR_OK);
    assert(tcp_retransmit_due(&pcb) == NET_ERR_TMO);
    assert(pcb.retry_count == 5);
    assert(pcb.error == NET_ERR_TMO);
    assert(pcb.outstanding == 0);
    assert(tcp_wait_connect(&pcb, -1) == NET_ERR_TMO);
    assert(tcp_connect_start(&pcb, netif, remote, 4800) == NET_ERR_OK);
    assert(sys_sem_wait(pcb.connect_done, -1) == NET_ERR_TMO);
    assert(tcp_close(&pcb) == NET_ERR_OK);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
}

static void test_receive_notification_is_binary(netif_t *netif,
                                                const ipaddr_t *remote)
{
    static const uint8_t first[] = "abc";
    static const uint8_t second[] = "def";
    tcp_pcb_t pcb = { 0 };
    uint8_t received[sizeof(first) + sizeof(second)] = { 0 };

    establish(&pcb, netif, remote, 4801);
    uint32_t seq = pcb.rcv_nxt;
    input_ok(netif, remote, &netif->ipaddr,
             make_segment(remote, &netif->ipaddr, 4801, pcb.local_port,
                          seq, pcb.snd_nxt, TCP_FLAG_ACK, first,
                          (int)sizeof(first)));
    seq += sizeof(first);
    input_ok(netif, remote, &netif->ipaddr,
             make_segment(remote, &netif->ipaddr, 4801, pcb.local_port,
                          seq, pcb.snd_nxt, TCP_FLAG_ACK, second,
                          (int)sizeof(second)));
    assert(tcp_recv_bytes(&pcb, received, sizeof(received), -1) ==
           (int)sizeof(received));
    assert(memcmp(received, first, sizeof(first)) == 0);
    assert(memcmp(received + sizeof(first), second, sizeof(second)) == 0);
    net_time_t started;
    sys_time_curr(&started);
    assert(tcp_recv_bytes(&pcb, received, sizeof(received), 10) == NET_ERR_TMO);
    assert(sys_time_goes(&started) >= 5);
    assert(tcp_close(&pcb) == NET_ERR_OK);
    assert(tcp_close(&pcb) == NET_ERR_OK);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
}

typedef struct _recv_thread_arg_t {
    tcp_pcb_t *pcb;
    int result;
} recv_thread_arg_t;

static void *connect_thread(void *arg)
{
    recv_thread_arg_t *thread_arg = arg;

    thread_arg->result = tcp_wait_connect(thread_arg->pcb, 0);
    return 0;
}

static void *close_thread(void *arg)
{
    recv_thread_arg_t *thread_arg = arg;

    thread_arg->result = tcp_wait_close(thread_arg->pcb, 0);
    return 0;
}

static void test_close_wakes_connect_waiter(netif_t *netif,
                                            const ipaddr_t *remote)
{
    tcp_pcb_t pcb = { 0 };
    recv_thread_arg_t arg = { .pcb = &pcb, .result = 0 };
    pthread_t thread;

    assert(tcp_open(&pcb) == NET_ERR_OK);
    assert(tcp_connect_start(&pcb, netif, remote, 4802) == NET_ERR_OK);
    assert(pthread_create(&thread, 0, connect_thread, &arg) == 0);
    for (;;) {
        nlocker_lock(&pcb.state_locker);
        int waiting = pcb.connect_waiters != 0;
        nlocker_unlock(&pcb.state_locker);
        if (waiting)
            break;
        sched_yield();
    }
    assert(tcp_close(&pcb) == NET_ERR_OK);
    assert(pthread_join(thread, 0) == 0);
    assert(arg.result == NET_ERR_STATE);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    assert(!pcb.opened);
    assert(tcp_wait_connect(&pcb, -1) == NET_ERR_PARAM);
}

static void *recv_thread(void *arg)
{
    recv_thread_arg_t *thread_arg = arg;
    uint8_t byte;

    thread_arg->result = tcp_recv_bytes(thread_arg->pcb, &byte, 1, 0);
    return 0;
}

static void test_close_wakes_receive_waiter(netif_t *netif,
                                            const ipaddr_t *remote)
{
    tcp_pcb_t pcb = { 0 };
    recv_thread_arg_t arg = { .pcb = &pcb, .result = 0 };
    pthread_t thread;

    assert(tcp_open(&pcb) == NET_ERR_OK);
    assert(tcp_connect_start(&pcb, netif, remote, 4804) == NET_ERR_OK);
    assert(pthread_create(&thread, 0, recv_thread, &arg) == 0);
    for (;;) {
        nlocker_lock(&pcb.state_locker);
        int waiting = pcb.recv_waiters != 0;
        nlocker_unlock(&pcb.state_locker);
        if (waiting)
            break;
        sched_yield();
    }
    assert(tcp_close(&pcb) == NET_ERR_OK);
    assert(pthread_join(thread, 0) == 0);
    assert(arg.result == NET_ERR_STATE);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    assert(!pcb.opened);
}

static void test_abort_wakes_close_waiter(netif_t *netif,
                                          const ipaddr_t *remote)
{
    tcp_pcb_t pcb = { 0 };
    recv_thread_arg_t arg = { .pcb = &pcb, .result = 0 };
    pthread_t thread;

    establish(&pcb, netif, remote, 4805);
    assert(tcp_close(&pcb) == NET_ERR_OK);
    assert(pthread_create(&thread, 0, close_thread, &arg) == 0);
    for (;;) {
        nlocker_lock(&pcb.state_locker);
        int waiting = pcb.close_waiters != 0;
        nlocker_unlock(&pcb.state_locker);
        if (waiting)
            break;
        sched_yield();
    }
    assert(tcp_close(&pcb) == NET_ERR_OK);
    assert(pthread_join(thread, 0) == 0);
    assert(arg.result == NET_ERR_STATE);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    assert(!pcb.opened);
}

static void test_handshake_and_receive(netif_t *netif,
                                       const ipaddr_t *remote)
{
    static const uint8_t payload[] = "stream";
    tcp_pcb_t pcb = { 0 };
    uint8_t received[sizeof(payload)] = { 0 };

    establish(&pcb, netif, remote, 4800);
    int before_zero_window = output_count;
    pcb.window = 0;
    assert(tcp_send_start(&pcb, payload, (int)sizeof(payload)) == NET_ERR_FULL);
    assert(output_count == before_zero_window);
    pcb.window = sizeof(payload) - 1;
    assert(tcp_send_start(&pcb, payload, (int)sizeof(payload)) == NET_ERR_FULL);
    assert(output_count == before_zero_window);
    pcb.window = 4096;
    assert(tcp_send_start(&pcb, payload, (int)sizeof(payload)) == NET_ERR_OK);
    assert(pcb.outstanding != 0);
    uint32_t first_seq = pcb.rcv_nxt;
    pktbuf_t *exact = make_segment(remote, &netif->ipaddr, 4800,
                                   pcb.local_port, first_seq, pcb.snd_nxt,
                                   TCP_FLAG_PSH | TCP_FLAG_ACK,
                                   payload, (int)sizeof(payload));
    input_ok(netif, remote, &netif->ipaddr, exact);
    assert(pcb.outstanding == 0);
    assert(pcb.rcv_nxt == first_seq + sizeof(payload));
    assert(tcp_recv_bytes(&pcb, received, sizeof(received), -1) ==
           (int)sizeof(payload));
    assert(memcmp(received, payload, sizeof(payload)) == 0);

    pktbuf_t *duplicate = make_segment(remote, &netif->ipaddr, 4800,
                                       pcb.local_port, first_seq,
                                       pcb.snd_nxt, TCP_FLAG_ACK,
                                       payload, (int)sizeof(payload));
    input_ok(netif, remote, &netif->ipaddr, duplicate);
    pktbuf_t *future = make_segment(remote, &netif->ipaddr, 4800,
                                    pcb.local_port, pcb.rcv_nxt + 10U,
                                    pcb.snd_nxt, TCP_FLAG_ACK,
                                    payload, (int)sizeof(payload));
    input_ok(netif, remote, &netif->ipaddr, future);
    assert(tcp_recv_bytes(&pcb, received, sizeof(received), 10) == NET_ERR_TMO);
    assert(tcp_close(&pcb) == NET_ERR_OK);
    assert(pcb.state == TCP_STATE_FIN_WAIT_1);

    pktbuf_t *fin_ack = make_segment(remote, &netif->ipaddr, 4800,
                                     pcb.local_port, pcb.rcv_nxt,
                                     pcb.snd_nxt, TCP_FLAG_ACK, 0, 0);
    input_ok(netif, remote, &netif->ipaddr, fin_ack);
    assert(pcb.state == TCP_STATE_FIN_WAIT_2);
    pktbuf_t *fin = make_segment(remote, &netif->ipaddr, 4800,
                                 pcb.local_port, pcb.rcv_nxt, pcb.snd_nxt,
                                 TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0);
    input_ok(netif, remote, &netif->ipaddr, fin);
    assert(pcb.state == TCP_STATE_TIME_WAIT);
    assert(tcp_close(&pcb) == NET_ERR_OK);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
}

static void test_time_wait_expires(netif_t *netif, const ipaddr_t *remote)
{
    tcp_pcb_t pcb = { 0 };
    recv_thread_arg_t arg = { .pcb = &pcb, .result = 0 };
    pthread_t thread;

    establish(&pcb, netif, remote, 4803);
    assert(tcp_close(&pcb) == NET_ERR_OK);
    input_ok(netif, remote, &netif->ipaddr,
             make_segment(remote, &netif->ipaddr, 4803, pcb.local_port,
                          pcb.rcv_nxt, pcb.snd_nxt, TCP_FLAG_ACK, 0, 0));
    input_ok(netif, remote, &netif->ipaddr,
             make_segment(remote, &netif->ipaddr, 4803, pcb.local_port,
                          pcb.rcv_nxt, pcb.snd_nxt,
                          TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0));
    assert(pcb.state == TCP_STATE_TIME_WAIT);
    int before_duplicate = output_count;
    input_ok(netif, remote, &netif->ipaddr,
             make_segment(remote, &netif->ipaddr, 4803, pcb.local_port,
                          pcb.rcv_nxt - 1U, pcb.snd_nxt,
                          TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0));
    assert(output_count == before_duplicate + 1);
    assert(pcb.state == TCP_STATE_TIME_WAIT);
    assert(pthread_create(&thread, 0, close_thread, &arg) == 0);
    for (;;) {
        nlocker_lock(&pcb.state_locker);
        int waiting = pcb.close_waiters != 0;
        nlocker_unlock(&pcb.state_locker);
        if (waiting)
            break;
        sched_yield();
    }
    assert(net_timer_check_tmo(TCP_TIME_WAIT_MS) == NET_ERR_OK);
    assert(pthread_join(thread, 0) == 0);
    assert(arg.result == NET_ERR_OK);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    assert(!pcb.opened);
    assert(tcp_wait_close(&pcb, -1) == NET_ERR_PARAM);
    tcp_pcb_t reused = { 0 };
    assert(tcp_open(&reused) == NET_ERR_OK);
    assert(tcp_close(&reused) == NET_ERR_OK);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
}

static void test_simultaneous_close(netif_t *netif,
                                    const ipaddr_t *remote)
{
    tcp_pcb_t pcb = { 0 };

    establish(&pcb, netif, remote, 4806);
    assert(tcp_close(&pcb) == NET_ERR_OK);
    assert(pcb.state == TCP_STATE_FIN_WAIT_1);

    input_ok(netif, remote, &netif->ipaddr,
             make_segment(remote, &netif->ipaddr, 4806, pcb.local_port,
                          pcb.rcv_nxt, pcb.snd_una,
                          TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0));
    assert(pcb.state == TCP_STATE_FIN_WAIT_1);
    assert(pcb.peer_fin_seen);

    input_ok(netif, remote, &netif->ipaddr,
             make_segment(remote, &netif->ipaddr, 4806, pcb.local_port,
                          pcb.rcv_nxt, pcb.snd_nxt, TCP_FLAG_ACK, 0, 0));
    assert(pcb.state == TCP_STATE_TIME_WAIT);
    assert(net_timer_check_tmo(TCP_TIME_WAIT_MS) == NET_ERR_OK);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    assert(!pcb.opened);
}

int main(void)
{
    ipaddr_t remote;

    assert(net_sys_init() == NET_ERR_OK);
    assert(pktbuf_init() == NET_ERR_OK);
    assert(net_timer_init() == NET_ERR_OK);
    assert(netif_init() == NET_ERR_OK);
    assert(ether_init() == NET_ERR_OK);
    assert(ipv4_init() == NET_ERR_OK);
    assert(tcp_init() == NET_ERR_OK);
    netif_t *netif = make_netif();
    assert(ipaddr_from_str(&remote, "192.0.2.20") == NET_ERR_OK);

    test_open_close_restores_slots();
    test_connect_allocation_rollback(netif, &remote);
    test_connect_and_retransmit(netif, &remote);
    test_receive_notification_is_binary(netif, &remote);
    test_close_wakes_connect_waiter(netif, &remote);
    test_close_wakes_receive_waiter(netif, &remote);
    test_abort_wakes_close_waiter(netif, &remote);
    test_handshake_and_receive(netif, &remote);
    test_time_wait_expires(netif, &remote);
    test_simultaneous_close(netif, &remote);

    assert(netif_set_deactive(netif) == NET_ERR_OK);
    assert(netif_close(netif) == NET_ERR_OK);
    return 0;
}
