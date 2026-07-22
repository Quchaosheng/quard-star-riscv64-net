#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <timeros/net/ipv4.h>
#include <timeros/net/net_sys.h>
#include <timeros/net/protocol.h>
#include <timeros/net/tcp.h>
#include <timeros/net/timer.h>
#include <timeros/net/tools.h>
#include <timeros/selftest.h>

typedef struct _captured_segment_t {
    uint16_t src_port;
    uint16_t dest_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t flags;
    int payload_size;
    int checksum_ok;
} captured_segment_t;

static captured_segment_t output;
static int output_count;
static net_err_t next_output_error;
static int child_close_markers;
static int listener_close_markers;
static int echo_complete_markers;
static int echo_retrans_markers;

void m6c2_mark_tcp_listen(void) { }
void m6c2_mark_tcp_accept(void) { }
void m6c2_mark_tcp_echo_complete(void) { echo_complete_markers++; }
void m6c2_mark_tcp_echo(void) { echo_retrans_markers++; }
void m6c2_mark_tcp_child_close(void) { child_close_markers++; }
void m6c2_mark_tcp_listener_close(void) { listener_close_markers++; }

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
    output.src_port = x_ntohs(header->src_port);
    output.dest_port = x_ntohs(header->dest_port);
    output.seq = x_ntohl(header->seq);
    output.ack = x_ntohl(header->ack);
    output.flags = header->flags;
    output.payload_size = pktbuf_total(buf) -
                          ((header->data_offset >> 4) * 4);
    output.checksum_ok = tcp_checksum(buf, &netif->ipaddr, dest,
                                      (uint16_t)pktbuf_total(buf)) == 0;
    output_count++;
    pktbuf_free(buf);
    if (next_output_error < 0) {
        net_err_t error = next_output_error;
        next_output_error = NET_ERR_OK;
        return error;
    }
    return NET_ERR_OK;
}

static void make_netif(netif_t *netif)
{
    memset(netif, 0, sizeof(*netif));
    netif->state = NETIF_ACTIVE;
    assert(ipaddr_from_str(&netif->ipaddr, "192.0.2.10") == NET_ERR_OK);
}

static void make_remote(ipaddr_t *remote, int host)
{
    char address[] = "192.0.2.20";

    assert(host >= 20 && host <= 29);
    address[8] = (char)('0' + host / 10);
    address[9] = (char)('0' + host % 10);
    assert(ipaddr_from_str(remote, address) == NET_ERR_OK);
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
    header->checksum = x_htons(tcp_checksum(buf, src, dest,
                                            (uint16_t)total));
    pktbuf_reset_acc(buf);
    return buf;
}

static net_err_t input(netif_t *netif, const ipaddr_t *remote,
                       uint16_t remote_port, uint32_t seq, uint32_t ack,
                       uint8_t flags)
{
    pktbuf_t *buf = make_segment(remote, &netif->ipaddr, remote_port, 8080,
                                 seq, ack, flags, 0, 0);
    net_err_t err = tcp_in(netif, remote, &netif->ipaddr, buf);

    if (err < 0)
        pktbuf_free(buf);
    return err;
}

static tcp_pcb_t *make_listener(netif_t *netif, int backlog)
{
    tcp_pcb_t *listener = 0;

    assert(tcp_open(&listener) == NET_ERR_OK);
    assert(tcp_bind(listener, netif, ipaddr_get_any(), 8080) == NET_ERR_OK);
    assert(tcp_listen(listener, backlog) == NET_ERR_OK);
    assert(listener->state == TCP_STATE_LISTEN);
    assert(listener->bound && !listener->passive);
    return listener;
}

static uint32_t send_syn(netif_t *netif, const ipaddr_t *remote,
                         uint16_t port, uint32_t peer_isn)
{
    int before = output_count;

    assert(input(netif, remote, port, peer_isn, 0, TCP_FLAG_SYN) ==
           NET_ERR_OK);
    assert(output_count == before + 1);
    assert(output.src_port == 8080);
    assert(output.dest_port == port);
    assert(output.ack == peer_isn + 1U);
    assert(output.flags == (TCP_FLAG_SYN | TCP_FLAG_ACK));
    assert(output.payload_size == 0);
    assert(output.checksum_ok);
    return output.seq;
}

static tcp_pcb_t *complete_handshake(tcp_pcb_t *listener, netif_t *netif,
                                     const ipaddr_t *remote, uint16_t port,
                                     uint32_t peer_isn)
{
    uint32_t server_isn = send_syn(netif, remote, port, peer_isn);

    assert(input(netif, remote, port, peer_isn + 1U, server_isn + 1U,
                 TCP_FLAG_ACK) == NET_ERR_OK);
    assert(tcp_accept_acquire(listener) == NET_ERR_OK);
    tcp_pcb_t *child = 0;
    ipaddr_t accepted_ip;
    uint16_t accepted_port = 0;
    assert(tcp_accept_peek_acquired(listener, &child, &accepted_ip,
                                    &accepted_port, -1) == NET_ERR_OK);
    assert(child != 0 && child->state == TCP_STATE_ESTABLISHED);
    assert(child->passive && !child->bound);
    assert(ipaddr_is_equal(&accepted_ip, remote));
    assert(accepted_port == port);
    return child;
}

static void queue_handshake(netif_t *netif, const ipaddr_t *remote,
                            uint16_t port, uint32_t peer_isn)
{
    uint32_t server_isn = send_syn(netif, remote, port, peer_isn);

    assert(input(netif, remote, port, peer_isn + 1U, server_isn + 1U,
                 TCP_FLAG_ACK) == NET_ERR_OK);
}

static void close_attached(tcp_pcb_t *pcb)
{
    assert(pcb->socket_attached);
    assert(tcp_close(pcb) == NET_ERR_OK);
    assert(tcp_close(pcb) == NET_ERR_OK);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    assert(pcb->opened);
    assert(tcp_socket_detach(pcb) == NET_ERR_OK);
    assert(!pcb->opened);
}

static void release_all_slots(void)
{
    tcp_pcb_t *slots[TCP_PCB_MAX] = { 0 };

    for (int i = 0; i < TCP_PCB_MAX; i++)
        assert(tcp_open(&slots[i]) == NET_ERR_OK);
    for (int i = 0; i < TCP_PCB_MAX; i++)
        assert(tcp_close(slots[i]) == NET_ERR_OK);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
}

static void assert_packet_pool_free(void)
{
    pktbuf_t *packets[PKTBUF_BUF_CNT] = { 0 };

    for (int i = 0; i < PKTBUF_BUF_CNT; i++) {
        packets[i] = pktbuf_alloc(0);
        assert(packets[i] != 0);
    }
    assert(pktbuf_alloc(0) == 0);
    for (int i = 0; i < PKTBUF_BUF_CNT; i++)
        pktbuf_free(packets[i]);
}

static void test_listen_handshake_and_accept(netif_t *netif)
{
    ipaddr_t remote;
    make_remote(&remote, 20);
    int before = output_count;
    int child_before = child_close_markers;
    int listener_before = listener_close_markers;
    tcp_pcb_t *listener = make_listener(netif, 4);
    assert(output_count == before);

    uint32_t server_isn = send_syn(netif, &remote, 5000, 1000);
    assert(server_isn == 1000);
    int after_syn = output_count;
    assert(input(netif, &remote, 5000, 1000, 0, TCP_FLAG_SYN) == NET_ERR_OK);
    assert(output_count == after_syn + 1);
    assert(output.seq == server_isn);
    assert(listener->pending_count == 1 && listener->accept_count == 0);

    assert(input(netif, &remote, 5000, 1001, server_isn + 2U,
                 TCP_FLAG_ACK) == NET_ERR_STATE);
    assert(tcp_accept_acquire(listener) == NET_ERR_OK);
    tcp_pcb_t *child = 0;
    assert(tcp_accept_peek_acquired(listener, &child, 0, 0, -1) ==
           NET_ERR_NONE);
    assert(tcp_accept_release_acquired(listener) == NET_ERR_OK);

    assert(input(netif, &remote, 5000, 1001, server_isn + 1U,
                 TCP_FLAG_ACK) == NET_ERR_OK);
    assert(tcp_accept_acquire(listener) == NET_ERR_OK);
    ipaddr_t accepted_ip;
    uint16_t accepted_port = 0;
    assert(tcp_accept_peek_acquired(listener, &child, &accepted_ip,
                                    &accepted_port, -1) == NET_ERR_OK);
    assert(child != 0 && child->state == TCP_STATE_ESTABLISHED);
    assert(ipaddr_is_equal(&accepted_ip, &remote));
    assert(accepted_port == 5000);
    assert(tcp_accept_acquire(listener) == NET_ERR_OK);
    tcp_pcb_t *second_peek = 0;
    assert(tcp_accept_peek_acquired(listener, &second_peek, 0, 0, -1) ==
           NET_ERR_OK);
    assert(second_peek == child && child->accept_pins == 2);
    assert(tcp_accept_commit_acquired(listener, listener) == NET_ERR_STATE);
    assert(listener->accept_count == 1 && listener->pending_count == 1);
    assert(listener->accept_waiters == 2 && child->accept_pins == 2);
    assert(child->listener == listener && !child->socket_attached);
    assert(tcp_accept_commit_acquired(listener, child) == NET_ERR_OK);
    assert(listener->accept_count == 0 && listener->pending_count == 0);
    assert(child->listener == 0);
    assert(child->socket_attached);
    assert(listener->accept_waiters == 1 && child->accept_pins == 1);
    assert(tcp_accept_commit_acquired(listener, child) == NET_ERR_STATE);
    assert(listener->accept_waiters == 1 && child->accept_pins == 1);
    assert(tcp_accept_release_child_acquired(listener, child) == NET_ERR_OK);
    assert(listener->accept_waiters == 0 && child->accept_pins == 0);

    close_attached(child);
    assert(child_close_markers == child_before + 1);
    assert(listener_close_markers == listener_before);
    assert(tcp_close(listener) == NET_ERR_OK);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    assert(listener_close_markers == listener_before + 1);
    assert(child_close_markers == child_before + 1);
}

static void test_passive_echo_completion(netif_t *netif)
{
    static const uint8_t first[] = "first";
    static const uint8_t second[] = "second";
    ipaddr_t remote;
    make_remote(&remote, 20);
    tcp_pcb_t *listener = make_listener(netif, 1);
    tcp_pcb_t *child = complete_handshake(listener, netif, &remote, 5050,
                                          1500);
    assert(tcp_accept_commit_acquired(listener, child) == NET_ERR_OK);

    int complete_before = echo_complete_markers;
    int retrans_before = echo_retrans_markers;
    assert(tcp_send_start(child, first, (int)sizeof(first)) == NET_ERR_OK);
    assert(input(netif, &remote, 5050, child->rcv_nxt,
                 child->outstanding_end, TCP_FLAG_ACK) == NET_ERR_OK);
    assert(echo_complete_markers == complete_before + 1);
    assert(echo_retrans_markers == retrans_before);

    assert(tcp_send_start(child, second, (int)sizeof(second)) == NET_ERR_OK);
    assert(tcp_retransmit_due(child) == NET_ERR_OK);
    assert(input(netif, &remote, 5050, child->rcv_nxt,
                 child->outstanding_end, TCP_FLAG_ACK) == NET_ERR_OK);
    assert(echo_complete_markers == complete_before + 2);
    assert(echo_retrans_markers == retrans_before + 1);

    close_attached(child);
    assert(tcp_close(listener) == NET_ERR_OK);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
}

static void test_backlog_limit(netif_t *netif)
{
    tcp_pcb_t *listener = make_listener(netif, 4);
    ipaddr_t remotes[5];

    for (int i = 0; i < 5; i++)
        make_remote(&remotes[i], 20 + i);
    for (int i = 0; i < 4; i++)
        (void)send_syn(netif, &remotes[i], (uint16_t)(5100 + i),
                       (uint32_t)(2000 + i));
    int before = output_count;
    assert(input(netif, &remotes[4], 5104, 2004, 0, TCP_FLAG_SYN) ==
           NET_ERR_FULL);
    assert(output_count == before);
    assert(listener->pending_count == 4);
    assert(tcp_close(listener) == NET_ERR_OK);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    release_all_slots();
}

static void test_half_open_retry_release(netif_t *netif)
{
    ipaddr_t remote;
    make_remote(&remote, 25);
    tcp_pcb_t *listener = make_listener(netif, 1);
    int child_before = child_close_markers;
    (void)send_syn(netif, &remote, 5200, 3000);

    for (int i = 0; i < TCP_RETRY_MAX; i++)
        assert(net_timer_check_tmo(TCP_RETRANS_MS) == NET_ERR_OK);
    assert(listener->pending_count == 0);
    assert(listener->accept_count == 0);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    assert(child_close_markers == child_before);
    assert_packet_pool_free();

    tcp_pcb_t *extra[TCP_PCB_MAX - 1] = { 0 };
    for (int i = 0; i < TCP_PCB_MAX - 1; i++)
        assert(tcp_open(&extra[i]) == NET_ERR_OK);
    for (int i = 0; i < TCP_PCB_MAX - 1; i++)
        assert(tcp_close(extra[i]) == NET_ERR_OK);
    assert(tcp_close(listener) == NET_ERR_OK);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
}

static void test_unpeeked_waiter_does_not_pin_half_open(netif_t *netif)
{
    ipaddr_t remote;
    make_remote(&remote, 25);
    tcp_pcb_t *listener = make_listener(netif, 1);
    assert(tcp_accept_acquire(listener) == NET_ERR_OK);

    tcp_pcb_t *held[TCP_PCB_MAX - 1] = { 0 };
    for (int i = 0; i < TCP_PCB_MAX - 1; i++)
        assert(tcp_open(&held[i]) == NET_ERR_OK);
    tcp_pcb_t *failed_child = held[4];
    assert(tcp_close(failed_child) == NET_ERR_OK);
    held[4] = 0;
    assert(net_timer_check_tmo(1) == NET_ERR_OK);

    (void)send_syn(netif, &remote, 5250, 3500);
    assert(failed_child->state == TCP_STATE_SYN_RECEIVED);
    for (int i = 0; i < TCP_RETRY_MAX; i++)
        assert(net_timer_check_tmo(TCP_RETRANS_MS) == NET_ERR_OK);
    assert(listener->pending_count == 0);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    assert_packet_pool_free();

    for (int i = 0; i < 3; i++) {
        tcp_pcb_t *reused = 0;
        assert(tcp_open(&reused) == NET_ERR_OK);
        assert(reused == failed_child);
        assert(tcp_close(reused) == NET_ERR_OK);
        assert(net_timer_check_tmo(1) == NET_ERR_OK);
    }
    assert(tcp_accept_release_acquired(listener) == NET_ERR_OK);
    for (int i = 0; i < TCP_PCB_MAX - 1; i++) {
        if (held[i] != 0)
            assert(tcp_close(held[i]) == NET_ERR_OK);
    }
    assert(tcp_close(listener) == NET_ERR_OK);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
}

static void test_listener_close_waiter_drain(netif_t *netif)
{
    ipaddr_t first;
    ipaddr_t second;
    make_remote(&first, 26);
    make_remote(&second, 27);
    tcp_pcb_t *listener = make_listener(netif, 2);
    (void)send_syn(netif, &first, 5300, 4000);
    uint32_t server_isn = send_syn(netif, &second, 5301, 5000);
    assert(input(netif, &second, 5301, 5001, server_isn + 1U,
                 TCP_FLAG_ACK) == NET_ERR_OK);
    assert(tcp_accept_acquire(listener) == NET_ERR_OK);

    assert(tcp_close(listener) == NET_ERR_OK);
    assert(listener->opened && listener->state == TCP_STATE_LISTEN);
    assert(listener->release_pending);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    assert(listener->opened);
    assert(tcp_accept_release_acquired(listener) == NET_ERR_OK);
    assert(tcp_close(listener) == NET_ERR_OK);
    assert(!listener->opened);
    release_all_slots();
}

static void test_direct_close_unaccepted_children(netif_t *netif)
{
    ipaddr_t first;
    ipaddr_t second;
    make_remote(&first, 28);
    make_remote(&second, 29);
    tcp_pcb_t *listener = make_listener(netif, 2);
    int child_before = child_close_markers;
    int listener_before = listener_close_markers;
    tcp_pcb_t *held[TCP_PCB_MAX - 1] = { 0 };
    for (int i = 0; i < TCP_PCB_MAX - 1; i++)
        assert(tcp_open(&held[i]) == NET_ERR_OK);
    tcp_pcb_t *half_open = held[3];
    assert(tcp_close(half_open) == NET_ERR_OK);
    held[3] = 0;
    assert(net_timer_check_tmo(1) == NET_ERR_OK);

    (void)send_syn(netif, &first, 5400, 6000);
    assert(half_open->listener == listener);
    assert(half_open->state == TCP_STATE_SYN_RECEIVED);
    assert(tcp_close(half_open) == NET_ERR_OK);
    assert(listener->pending_count == 0 && listener->accept_count == 0);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    assert(child_close_markers == child_before);

    tcp_pcb_t *child = complete_handshake(listener, netif, &second, 5401,
                                          7000);
    assert(tcp_accept_release_child_acquired(listener, child) == NET_ERR_OK);
    assert(listener->accept_count == 1 && listener->pending_count == 1);
    assert(tcp_close(child) == NET_ERR_OK);
    assert(listener->accept_count == 0 && listener->pending_count == 0);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    assert(!child->opened);
    assert(child_close_markers == child_before);
    for (int i = 0; i < TCP_PCB_MAX - 1; i++) {
        if (held[i] != 0)
            assert(tcp_close(held[i]) == NET_ERR_OK);
    }
    assert(tcp_close(listener) == NET_ERR_OK);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    assert(listener_close_markers == listener_before + 1);
    assert(child_close_markers == child_before);
    release_all_slots();
}

static void test_peek_pins_detached_child(netif_t *netif)
{
    ipaddr_t first;
    ipaddr_t replacement_remote;
    make_remote(&first, 26);
    make_remote(&replacement_remote, 27);
    tcp_pcb_t *listener = make_listener(netif, 1);
    int child_before = child_close_markers;
    queue_handshake(netif, &first, 5600, 9000);

    assert(tcp_accept_acquire(listener) == NET_ERR_OK);
    assert(tcp_accept_acquire(listener) == NET_ERR_OK);
    tcp_pcb_t *stale = 0;
    ipaddr_t snapshot;
    uint16_t snapshot_port = 0;
    assert(tcp_accept_peek_acquired(listener, &stale, &snapshot,
                                    &snapshot_port, -1) == NET_ERR_OK);
    tcp_pcb_t *same_child = 0;
    assert(tcp_accept_peek_acquired(listener, &same_child, 0, 0, -1) ==
           NET_ERR_OK);
    assert(same_child == stale && stale->accept_pins == 2);
    assert(ipaddr_is_equal(&snapshot, &first));
    assert(snapshot_port == 5600);

    assert(input(netif, &first, 5600, stale->rcv_nxt, stale->snd_nxt,
                 TCP_FLAG_FIN | TCP_FLAG_ACK) == NET_ERR_OK);
    assert(listener->accept_count == 0 && listener->pending_count == 0);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    assert(child_close_markers == child_before);

    tcp_pcb_t *held[TCP_PCB_MAX - 2] = { 0 };
    for (int i = 0; i < TCP_PCB_MAX - 2; i++) {
        assert(tcp_open(&held[i]) == NET_ERR_OK);
        assert(held[i] != stale);
    }
    tcp_pcb_t *probe = 0;
    assert(tcp_open(&probe) == NET_ERR_MEM);
    assert(probe == 0);
    assert(tcp_accept_commit_acquired(listener, stale) == NET_ERR_STATE);
    assert(listener->accept_waiters == 2 && stale->accept_pins == 2);

    assert(tcp_accept_release_child_acquired(listener, stale) == NET_ERR_OK);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    assert(stale->opened && stale->accept_pins == 1);
    assert(child_close_markers == child_before);
    assert(tcp_open(&probe) == NET_ERR_MEM);
    assert(tcp_accept_release_child_acquired(listener, stale) == NET_ERR_OK);
    assert(tcp_accept_release_child_acquired(listener, stale) < 0);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    assert(!stale->opened);
    assert(child_close_markers == child_before);

    queue_handshake(netif, &replacement_remote, 5601, 9100);
    assert(tcp_accept_acquire(listener) == NET_ERR_OK);
    tcp_pcb_t *replacement = 0;
    uint16_t replacement_port = 0;
    assert(tcp_accept_peek_acquired(listener, &replacement, 0,
                                    &replacement_port, -1) == NET_ERR_OK);
    assert(replacement == stale);
    assert(replacement_port == 5601);
    assert(tcp_accept_commit_acquired(listener, replacement) == NET_ERR_OK);
    close_attached(replacement);

    for (int i = 0; i < TCP_PCB_MAX - 2; i++)
        assert(tcp_close(held[i]) == NET_ERR_OK);
    assert(tcp_close(listener) == NET_ERR_OK);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    release_all_slots();
}

static void commit_next(tcp_pcb_t *listener, uint16_t expected_port)
{
    tcp_pcb_t *child = 0;
    uint16_t port = 0;

    assert(tcp_accept_acquire(listener) == NET_ERR_OK);
    assert(tcp_accept_peek_acquired(listener, &child, 0, &port, -1) ==
           NET_ERR_OK);
    assert(port == expected_port);
    assert(tcp_accept_commit_acquired(listener, child) == NET_ERR_OK);
    close_attached(child);
}

static void test_queued_fin_preserves_ring_order(netif_t *netif)
{
    ipaddr_t remotes[6];
    tcp_pcb_t *listener = make_listener(netif, 4);

    for (int i = 0; i < 6; i++)
        make_remote(&remotes[i], 20 + i);
    for (int i = 0; i < 3; i++)
        queue_handshake(netif, &remotes[i], (uint16_t)(5500 + i),
                        (uint32_t)(8000 + i * 100));

    tcp_pcb_t *middle = listener->accept_queue[1];
    assert(input(netif, &remotes[1], 5501, middle->rcv_nxt, middle->snd_nxt,
                 TCP_FLAG_FIN | TCP_FLAG_ACK) == NET_ERR_OK);
    assert(listener->accept_count == 2 && listener->pending_count == 2);

    queue_handshake(netif, &remotes[3], 5503, 8300);
    commit_next(listener, 5500);
    queue_handshake(netif, &remotes[4], 5504, 8400);
    queue_handshake(netif, &remotes[5], 5505, 8500);
    assert(listener->accept_count == 4 && listener->pending_count == 4);

    tcp_pcb_t *failed_middle = listener->accept_queue[
        (listener->accept_head + 1) % TCP_ACCEPT_MAX];
    next_output_error = NET_ERR_IO;
    assert(input(netif, &remotes[3], 5503, failed_middle->rcv_nxt,
                 failed_middle->snd_nxt,
                 TCP_FLAG_FIN | TCP_FLAG_ACK) == NET_ERR_IO);
    assert(listener->accept_count == 3 && listener->pending_count == 3);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);

    commit_next(listener, 5502);
    commit_next(listener, 5504);
    commit_next(listener, 5505);
    assert(listener->accept_count == 0 && listener->pending_count == 0);
    assert(tcp_close(listener) == NET_ERR_OK);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    release_all_slots();
}

int main(void)
{
    netif_t netif;

    assert(net_sys_init() == NET_ERR_OK);
    assert(pktbuf_init() == NET_ERR_OK);
    assert(net_timer_init() == NET_ERR_OK);
    assert(tcp_init() == NET_ERR_OK);
    make_netif(&netif);

    test_listen_handshake_and_accept(&netif);
    test_passive_echo_completion(&netif);
    test_backlog_limit(&netif);
    test_half_open_retry_release(&netif);
    test_unpeeked_waiter_does_not_pin_half_open(&netif);
    test_listener_close_waiter_drain(&netif);
    test_direct_close_unaccepted_children(&netif);
    test_peek_pins_detached_child(&netif);
    test_queued_fin_preserves_ring_order(&netif);
    return 0;
}
