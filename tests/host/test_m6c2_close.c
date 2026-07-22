#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <timeros/net/ipv4.h>
#include <timeros/net/net_sys.h>
#include <timeros/net/protocol.h>
#include <timeros/net/tcp.h>
#include <timeros/net/timer.h>
#include <timeros/net/tools.h>

typedef struct _captured_segment_t {
    uint32_t seq;
    uint32_t ack;
    uint8_t flags;
    int payload_size;
} captured_segment_t;

static captured_segment_t output;
static int output_count;

net_err_t ipv4_register_handler(uint8_t protocol,
                                ipv4_input_handler_t handler)
{
    assert(protocol == NET_PROTOCOL_TCP);
    assert(handler == tcp_in);
    return NET_ERR_OK;
}

net_err_t ipv4_out(netif_t *netif, const ipaddr_t *dest, uint8_t protocol,
                   pktbuf_t *buf)
{
    assert(netif != 0 && dest != 0 && protocol == NET_PROTOCOL_TCP);
    assert(pktbuf_set_cont(buf, TCP_HEADER_SIZE) == NET_ERR_OK);
    tcp_hdr_t *header = (tcp_hdr_t *)pktbuf_data(buf);
    int header_size = (header->data_offset >> 4) * 4;

    output.seq = x_ntohl(header->seq);
    output.ack = x_ntohl(header->ack);
    output.flags = header->flags;
    output.payload_size = pktbuf_total(buf) - header_size;
    output_count++;
    pktbuf_free(buf);
    return NET_ERR_OK;
}

static netif_t *make_netif(void)
{
    static netif_t netif;

    memset(&netif, 0, sizeof(netif));
    netif.state = NETIF_ACTIVE;
    assert(ipaddr_from_str(&netif.ipaddr, "192.0.2.10") == NET_ERR_OK);
    return &netif;
}

static pktbuf_t *make_segment(const ipaddr_t *src, const ipaddr_t *dest,
                              uint16_t src_port, uint16_t dest_port,
                              uint32_t seq, uint32_t ack, uint8_t flags)
{
    pktbuf_t *buf = pktbuf_alloc(TCP_HEADER_SIZE);

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
    header->checksum = x_htons(tcp_checksum(buf, src, dest,
                                            TCP_HEADER_SIZE));
    pktbuf_reset_acc(buf);
    return buf;
}

static net_err_t input(netif_t *netif, const ipaddr_t *remote,
                       uint16_t remote_port, tcp_pcb_t *pcb,
                       uint32_t seq, uint32_t ack, uint8_t flags)
{
    pktbuf_t *buf = make_segment(remote, &netif->ipaddr, remote_port,
                                 pcb->local_port, seq, ack, flags);
    net_err_t err = tcp_in(netif, remote, &netif->ipaddr, buf);

    if (err < 0)
        pktbuf_free(buf);
    return err;
}

static tcp_pcb_t *establish(netif_t *netif, const ipaddr_t *remote,
                            uint16_t remote_port, uint32_t peer_isn)
{
    tcp_pcb_t *pcb = 0;

    assert(tcp_open(&pcb) == NET_ERR_OK);
    assert(tcp_connect_start(pcb, netif, remote, remote_port) == NET_ERR_OK);
    assert(output.flags == TCP_FLAG_SYN);
    assert(input(netif, remote, remote_port, pcb, peer_isn,
                 pcb->iss + 1U, TCP_FLAG_SYN | TCP_FLAG_ACK) == NET_ERR_OK);
    assert(pcb->state == TCP_STATE_ESTABLISHED);
    assert(pcb->outstanding == 0);
    return pcb;
}

static void finish_close(netif_t *netif, const ipaddr_t *remote,
                         uint16_t remote_port, tcp_pcb_t *pcb)
{
    assert(input(netif, remote, remote_port, pcb, pcb->rcv_nxt,
                 pcb->snd_nxt, TCP_FLAG_ACK) == NET_ERR_OK);
    assert(pcb->state == TCP_STATE_FIN_WAIT_2);
    assert(input(netif, remote, remote_port, pcb, pcb->rcv_nxt,
                 pcb->snd_nxt, TCP_FLAG_FIN | TCP_FLAG_ACK) == NET_ERR_OK);
    assert(pcb->state == TCP_STATE_TIME_WAIT);
    assert(net_timer_check_tmo(TCP_TIME_WAIT_MS) == NET_ERR_OK);
}

static void test_close_waits_for_complete_data_ack(netif_t *netif,
                                                   const ipaddr_t *remote)
{
    static const uint8_t payload[] = "echo";
    tcp_pcb_t *pcb = establish(netif, remote, 5000, 7000);
    tcp_pcb_t *expected_slot = pcb;
    int before_data = output_count;

    assert(tcp_send_start(pcb, payload, (int)sizeof(payload)) == NET_ERR_OK);
    assert(output_count == before_data + 1);
    assert(output.flags == (TCP_FLAG_PSH | TCP_FLAG_ACK));
    uint32_t data_seq = output.seq;
    uint32_t data_end = pcb->outstanding_end;
    assert(data_end == data_seq + sizeof(payload));

    int after_data = output_count;
    assert(tcp_close(pcb) == NET_ERR_OK);
    assert(tcp_close(pcb) == NET_ERR_OK);
    assert(pcb->state == TCP_STATE_ESTABLISHED);
    assert(pcb->close_requested);
    assert(pcb->outstanding != 0);
    assert(output_count == after_data);
    assert(tcp_wait_close_acquire(pcb) == NET_ERR_OK);
    assert(pcb->close_waiters == 1);

    assert(input(netif, remote, 5000, pcb, pcb->rcv_nxt,
                 data_end - 1U, TCP_FLAG_ACK) == NET_ERR_OK);
    assert(pcb->state == TCP_STATE_ESTABLISHED);
    assert(pcb->close_requested);
    assert(pcb->outstanding != 0);
    assert(output_count == after_data);

    assert(input(netif, remote, 5000, pcb, pcb->rcv_nxt,
                 data_end, TCP_FLAG_ACK) == NET_ERR_OK);
    assert(output_count == after_data + 1);
    assert(output.flags == (TCP_FLAG_FIN | TCP_FLAG_ACK));
    assert(output.payload_size == 0);
    assert(output.seq == data_end);
    assert(!pcb->close_requested);
    assert(pcb->state == TCP_STATE_FIN_WAIT_1);

    finish_close(netif, remote, 5000, pcb);
    assert(tcp_wait_close_acquired(pcb, -1) == NET_ERR_OK);
    assert(pcb->close_waiters == 0);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    assert(!pcb->opened);

    tcp_pcb_t *reused = 0;
    assert(tcp_open(&reused) == NET_ERR_OK);
    assert(reused == expected_slot);
    assert(tcp_close(reused) == NET_ERR_OK);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
}

static void test_retry_exhaustion_wakes_close_waiter(netif_t *netif,
                                                     const ipaddr_t *remote)
{
    static const uint8_t payload[] = "lost echo";
    tcp_pcb_t *pcb = establish(netif, remote, 5001, 8000);
    tcp_pcb_t *expected_slot = pcb;

    assert(tcp_send_start(pcb, payload, (int)sizeof(payload)) == NET_ERR_OK);
    assert(output.flags == (TCP_FLAG_PSH | TCP_FLAG_ACK));
    int after_data = output_count;
    assert(tcp_close(pcb) == NET_ERR_OK);
    assert(pcb->close_requested);
    assert(output_count == after_data);
    assert(tcp_wait_close_acquire(pcb) == NET_ERR_OK);

    for (int i = 1; i < TCP_RETRY_MAX; i++) {
        assert(tcp_retransmit_due(pcb) == NET_ERR_OK);
        assert(output.flags == (TCP_FLAG_PSH | TCP_FLAG_ACK));
        assert(output_count == after_data + i);
    }
    assert(tcp_retransmit_due(pcb) == NET_ERR_TMO);
    assert(output.flags == (TCP_FLAG_PSH | TCP_FLAG_ACK));
    assert(output_count == after_data + TCP_RETRY_MAX);
    assert(pcb->state == TCP_STATE_CLOSED);
    assert(pcb->outstanding == 0);
    assert(!pcb->close_requested);
    assert(tcp_wait_close_acquired(pcb, -1) == NET_ERR_TMO);
    assert(pcb->close_waiters == 0);

    assert(tcp_close(pcb) == NET_ERR_OK);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    assert(!pcb->opened);
    tcp_pcb_t *reused = 0;
    assert(tcp_open(&reused) == NET_ERR_OK);
    assert(reused == expected_slot);
    assert(tcp_close(reused) == NET_ERR_OK);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
}

static void test_peer_fin_waits_for_complete_data_ack(netif_t *netif,
                                                      const ipaddr_t *remote)
{
    static const uint8_t payload[] = "final echo";
    tcp_pcb_t *pcb = establish(netif, remote, 5002, 9000);
    tcp_pcb_t *expected_slot = pcb;

    assert(tcp_send_start(pcb, payload, (int)sizeof(payload)) == NET_ERR_OK);
    pktbuf_t *outstanding = pcb->outstanding;
    uint32_t data_end = pcb->outstanding_end;
    assert(tcp_close(pcb) == NET_ERR_OK);
    assert(tcp_wait_close_acquire(pcb) == NET_ERR_OK);
    int before_fin = output_count;

    assert(input(netif, remote, 5002, pcb, pcb->rcv_nxt,
                 data_end - 1U, TCP_FLAG_FIN | TCP_FLAG_ACK) == NET_ERR_OK);
    assert(output_count == before_fin + 1);
    assert(output.flags == TCP_FLAG_ACK);
    assert(output.payload_size == 0);
    assert(pcb->state == TCP_STATE_ESTABLISHED);
    assert(pcb->peer_fin_seen);
    assert(pcb->close_requested);
    assert(pcb->outstanding == outstanding);
    assert(pcb->outstanding_end == data_end);

    assert(input(netif, remote, 5002, pcb, pcb->rcv_nxt,
                 data_end, TCP_FLAG_ACK) == NET_ERR_OK);
    assert(output_count == before_fin + 2);
    assert(output.flags == (TCP_FLAG_FIN | TCP_FLAG_ACK));
    assert(output.seq == data_end);
    assert(pcb->state == TCP_STATE_FIN_WAIT_1);
    assert(pcb->peer_fin_seen);
    assert(!pcb->close_requested);

    assert(input(netif, remote, 5002, pcb, pcb->rcv_nxt,
                 pcb->snd_nxt, TCP_FLAG_ACK) == NET_ERR_OK);
    assert(pcb->state == TCP_STATE_TIME_WAIT);
    assert(!pcb->peer_fin_seen);
    assert(net_timer_check_tmo(TCP_TIME_WAIT_MS) == NET_ERR_OK);
    assert(tcp_wait_close_acquired(pcb, -1) == NET_ERR_OK);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    assert(!pcb->opened);

    tcp_pcb_t *reused = 0;
    assert(tcp_open(&reused) == NET_ERR_OK);
    assert(reused == expected_slot);
    assert(tcp_close(reused) == NET_ERR_OK);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
}

int main(void)
{
    ipaddr_t remote;

    assert(net_sys_init() == NET_ERR_OK);
    assert(pktbuf_init() == NET_ERR_OK);
    assert(net_timer_init() == NET_ERR_OK);
    assert(tcp_init() == NET_ERR_OK);
    netif_t *netif = make_netif();
    assert(ipaddr_from_str(&remote, "192.0.2.20") == NET_ERR_OK);

    test_close_waits_for_complete_data_ack(netif, &remote);
    test_retry_exhaustion_wakes_close_waiter(netif, &remote);
    test_peer_fin_waits_for_complete_data_ack(netif, &remote);
    return 0;
}
