#define _XOPEN_SOURCE 700

#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>

#include <timeros/net/ipv4.h>
#include <timeros/net/net_sys.h>
#include <timeros/net/protocol.h>
#include <timeros/net/socket.h>
#include <timeros/net/timer.h>
#include <timeros/net/tools.h>

typedef struct _captured_segment_t {
    uint16_t src_port;
    uint16_t dest_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t flags;
    int payload_size;
} captured_segment_t;

static captured_segment_t output;
static pthread_barrier_t acquired_barrier;
static pthread_barrier_t unlocked_barrier;
static int pause_accept;

static void wait_barrier(pthread_barrier_t *barrier)
{
    int result = pthread_barrier_wait(barrier);

    assert(result == 0 || result == PTHREAD_BARRIER_SERIAL_THREAD);
}

void net_socket_test_waiter_acquired_hook(void)
{
    if (__atomic_load_n(&pause_accept, __ATOMIC_ACQUIRE))
        wait_barrier(&acquired_barrier);
}

void net_socket_test_waiter_unlocked_hook(void)
{
    if (__atomic_load_n(&pause_accept, __ATOMIC_ACQUIRE))
        wait_barrier(&unlocked_barrier);
}

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
    pktbuf_free(buf);
    return NET_ERR_OK;
}

static pktbuf_t *make_segment(const ipaddr_t *src, const ipaddr_t *dest,
                              uint16_t src_port, uint16_t dest_port,
                              uint32_t seq, uint32_t ack, uint8_t flags,
                              const uint8_t *data, int size)
{
    pktbuf_t *buf = pktbuf_alloc(TCP_HEADER_SIZE + size);

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
    header->checksum = x_htons(tcp_checksum(
        buf, src, dest, (uint16_t)(TCP_HEADER_SIZE + size)));
    pktbuf_reset_acc(buf);
    return buf;
}

static void input_ok(netif_t *netif, const ipaddr_t *remote,
                     uint16_t remote_port, uint16_t local_port,
                     uint32_t seq, uint32_t ack, uint8_t flags,
                     const uint8_t *data, int size)
{
    pktbuf_t *buf = make_segment(remote, &netif->ipaddr, remote_port,
                                 local_port, seq, ack, flags, data, size);
    assert(tcp_in(netif, remote, &netif->ipaddr, buf) == NET_ERR_OK);
}

static uint32_t queue_connection(netif_t *netif, const ipaddr_t *remote,
                                 uint16_t remote_port, uint16_t local_port,
                                 uint32_t peer_isn)
{
    input_ok(netif, remote, remote_port, local_port, peer_isn, 0,
             TCP_FLAG_SYN, 0, 0);
    assert(output.src_port == local_port && output.dest_port == remote_port);
    assert(output.flags == (TCP_FLAG_SYN | TCP_FLAG_ACK));
    assert(output.ack == peer_isn + 1U);
    uint32_t server_isn = output.seq;
    input_ok(netif, remote, remote_port, local_port, peer_isn + 1U,
             server_isn + 1U, TCP_FLAG_ACK, 0, 0);
    return server_isn;
}

static int make_listener(netif_t *netif, uint16_t port)
{
    int handle = net_socket_open(NET_SOCKET_TCP);

    assert(handle >= 0);
    assert(net_socket_bind(handle, netif, ipaddr_get_any(), port) ==
           NET_ERR_OK);
    assert(net_socket_listen(handle, 4) == NET_ERR_OK);
    assert(net_socket_listen(handle, 4) == NET_ERR_STATE);
    return handle;
}

static void close_listener(int handle)
{
    assert(net_socket_close(handle) == NET_ERR_NONE);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    assert(net_socket_close(handle) == NET_ERR_OK);
}

static void close_accepted(int handle, netif_t *netif,
                           const ipaddr_t *remote, uint16_t remote_port,
                           uint16_t local_port, uint32_t peer_seq)
{
    assert(net_socket_close(handle) == NET_ERR_NONE);
    assert(output.flags == (TCP_FLAG_FIN | TCP_FLAG_ACK));
    uint32_t fin_seq = output.seq;
    input_ok(netif, remote, remote_port, local_port, peer_seq, fin_seq + 1U,
             TCP_FLAG_ACK, 0, 0);
    input_ok(netif, remote, remote_port, local_port, peer_seq, fin_seq + 1U,
             TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0);
    assert(net_timer_check_tmo(TCP_TIME_WAIT_MS) == NET_ERR_OK);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    assert(net_socket_wait_close(handle, -1) == NET_ERR_OK);
    assert(net_socket_close(handle) == NET_ERR_OK);
}

typedef struct _accept_thread_arg_t {
    int handle;
    int result;
    net_socket_accept_t accept;
} accept_thread_arg_t;

static void *accept_thread(void *arg)
{
    accept_thread_arg_t *request = arg;

    int result = net_socket_accept_prepare(request->handle,
                                            &request->accept);
    __atomic_store_n(&request->result, result, __ATOMIC_RELEASE);
    return 0;
}

static void test_type_and_listener_guards(netif_t *netif,
                                          const ipaddr_t *remote)
{
    uint8_t byte = 1;
    net_socket_accept_t accept;
    int udp = net_socket_open(NET_SOCKET_UDP);

    assert(udp >= 0);
    memset(&accept, 0xa5, sizeof(accept));
    assert(net_socket_listen(udp, 1) == NET_ERR_PARAM);
    assert(net_socket_accept_prepare(udp, &accept) == NET_ERR_PARAM);
    assert(!accept.acquired && accept.listener == 0 && accept.child == 0);
    assert(net_socket_accept_commit(&accept) == NET_ERR_PARAM);
    net_socket_accept_abort(&accept);
    assert(net_socket_close(udp) == NET_ERR_OK);

    int listener = make_listener(netif, 8080);
    assert(net_socket_connect_start(listener, netif, remote, 5000) ==
           NET_ERR_STATE);
    assert(net_socket_send(listener, &byte, 1) == NET_ERR_STATE);
    assert(net_socket_recv(listener, &byte, 1, -1) == NET_ERR_STATE);
    close_listener(listener);
}

static void test_block_abort_commit_and_generation(netif_t *netif,
                                                    const ipaddr_t *remote)
{
    static const uint8_t sent[] = "server-data";
    static const uint8_t received_data[] = "client-data";
    uint8_t received[sizeof(received_data)] = { 0 };
    int listener = make_listener(netif, 8081);
    accept_thread_arg_t request = {
        .handle = listener,
        .result = INT_MIN,
    };
    pthread_t thread;

    pause_accept = 1;
    assert(pthread_barrier_init(&acquired_barrier, 0, 2) == 0);
    assert(pthread_barrier_init(&unlocked_barrier, 0, 2) == 0);
    assert(pthread_create(&thread, 0, accept_thread, &request) == 0);
    wait_barrier(&acquired_barrier);
    wait_barrier(&unlocked_barrier);
    assert(__atomic_load_n(&request.result, __ATOMIC_ACQUIRE) == INT_MIN);
    pause_accept = 0;
    uint32_t server_isn = queue_connection(netif, remote, 5001, 8081,
                                            7000);
    assert(pthread_join(thread, 0) == 0);
    assert(pthread_barrier_destroy(&unlocked_barrier) == 0);
    assert(pthread_barrier_destroy(&acquired_barrier) == 0);
    assert(request.result == NET_ERR_OK);
    assert(request.accept.acquired && request.accept.child != 0);
    assert(ipaddr_is_equal(&request.accept.remote_ip, remote));
    assert(request.accept.remote_port == 5001);

    tcp_pcb_t *first_child = request.accept.child;
    net_socket_accept_abort(&request.accept);
    assert(!request.accept.acquired && request.accept.listener == 0 &&
           request.accept.child == 0);
    net_socket_accept_abort(&request.accept);

    assert(net_socket_accept_prepare(listener, &request.accept) ==
           NET_ERR_OK);
    assert(request.accept.child == first_child);
    int first = net_socket_accept_commit(&request.accept);
    assert(first >= 0 && !request.accept.acquired);
    assert(net_socket_send(first, sent, sizeof(sent)) == NET_ERR_OK);
    assert(output.flags == (TCP_FLAG_PSH | TCP_FLAG_ACK));
    uint32_t data_seq = output.seq;
    input_ok(netif, remote, 5001, 8081, 7001,
             data_seq + sizeof(sent), TCP_FLAG_ACK, received_data,
             sizeof(received_data));
    assert(net_socket_recv(first, received, sizeof(received), -1) ==
           (int)sizeof(received_data));
    assert(memcmp(received, received_data, sizeof(received_data)) == 0);
    close_accepted(first, netif, remote, 5001, 8081,
                   7001U + sizeof(received_data));

    queue_connection(netif, remote, 5002, 8081, 8000);
    assert(net_socket_accept_prepare(listener, &request.accept) ==
           NET_ERR_OK);
    int second = net_socket_accept_commit(&request.accept);
    assert(second >= 0 && second != first);
    assert((second & 0xff) == (first & 0xff));
    close_accepted(second, netif, remote, 5002, 8081, 8001);
    (void)server_isn;
    close_listener(listener);
}

static void test_full_table_preserves_queue(netif_t *netif,
                                            const ipaddr_t *remote)
{
    int listener = make_listener(netif, 8082);
    queue_connection(netif, remote, 5100, 8082, 9000);
    int fillers[NET_SOCKET_MAX - 1];

    for (int i = 0; i < NET_SOCKET_MAX - 1; i++) {
        fillers[i] = net_socket_open(NET_SOCKET_UDP);
        assert(fillers[i] >= 0);
    }
    assert(net_socket_open(NET_SOCKET_UDP) == NET_ERR_FULL);
    net_socket_accept_t accept;
    assert(net_socket_accept_prepare(listener, &accept) == NET_ERR_OK);
    tcp_pcb_t *queued_child = accept.child;
    ipaddr_t queued_ip = accept.remote_ip;
    uint16_t queued_port = accept.remote_port;
    assert(net_socket_accept_commit(&accept) == NET_ERR_FULL);
    assert(!accept.acquired && accept.listener == 0 && accept.child == 0);

    assert(net_socket_close(fillers[0]) == NET_ERR_OK);
    assert(net_socket_accept_prepare(listener, &accept) == NET_ERR_OK);
    assert(accept.child == queued_child);
    assert(ipaddr_is_equal(&accept.remote_ip, &queued_ip));
    assert(accept.remote_port == queued_port);
    int child = net_socket_accept_commit(&accept);
    assert(child >= 0);
    close_accepted(child, netif, remote, 5100, 8082, 9001);
    for (int i = 1; i < NET_SOCKET_MAX - 1; i++)
        assert(net_socket_close(fillers[i]) == NET_ERR_OK);
    close_listener(listener);
}

static void test_listener_close_races(netif_t *netif,
                                      const ipaddr_t *remote)
{
    int listener = make_listener(netif, 8083);
    accept_thread_arg_t request = {
        .handle = listener,
        .result = INT_MIN,
    };
    pthread_t thread;

    pause_accept = 1;
    assert(pthread_barrier_init(&acquired_barrier, 0, 2) == 0);
    assert(pthread_barrier_init(&unlocked_barrier, 0, 2) == 0);
    assert(pthread_create(&thread, 0, accept_thread, &request) == 0);
    wait_barrier(&acquired_barrier);
    assert(net_socket_close(listener) == NET_ERR_NONE);
    wait_barrier(&unlocked_barrier);
    pause_accept = 0;
    assert(pthread_join(thread, 0) == 0);
    assert(request.result == NET_ERR_STATE);
    assert(!request.accept.acquired && request.accept.listener == 0 &&
           request.accept.child == 0);
    assert(pthread_barrier_destroy(&unlocked_barrier) == 0);
    assert(pthread_barrier_destroy(&acquired_barrier) == 0);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    assert(net_socket_close(listener) == NET_ERR_OK);

    listener = make_listener(netif, 8084);
    queue_connection(netif, remote, 5200, 8084, 10000);
    assert(net_socket_accept_prepare(listener, &request.accept) ==
           NET_ERR_OK);
    assert(net_socket_close(listener) == NET_ERR_NONE);
    assert(net_socket_accept_commit(&request.accept) == NET_ERR_STATE);
    assert(!request.accept.acquired && request.accept.child == 0);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    assert(net_socket_close(listener) == NET_ERR_OK);
}

static void test_pool_reuse(void)
{
    int handles[TCP_PCB_MAX];

    for (int i = 0; i < TCP_PCB_MAX; i++) {
        handles[i] = net_socket_open(NET_SOCKET_TCP);
        assert(handles[i] >= 0);
    }
    assert(net_socket_open(NET_SOCKET_TCP) == NET_ERR_MEM);
    for (int i = 0; i < TCP_PCB_MAX; i++)
        assert(net_socket_close(handles[i]) == NET_ERR_NONE);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    for (int i = 0; i < TCP_PCB_MAX; i++)
        assert(net_socket_close(handles[i]) == NET_ERR_OK);
}

int main(void)
{
    netif_t netif = { 0 };
    ipaddr_t remote;

    assert(net_sys_init() == NET_ERR_OK);
    assert(pktbuf_init() == NET_ERR_OK);
    assert(net_timer_init() == NET_ERR_OK);
    assert(tcp_init() == NET_ERR_OK);
    assert(net_socket_init() == NET_ERR_OK);
    netif.state = NETIF_ACTIVE;
    assert(ipaddr_from_str(&netif.ipaddr, "192.0.2.10") == NET_ERR_OK);
    assert(ipaddr_from_str(&remote, "192.0.2.20") == NET_ERR_OK);

    test_type_and_listener_guards(&netif, &remote);
    test_block_abort_commit_and_generation(&netif, &remote);
    test_full_table_preserves_queue(&netif, &remote);
    test_listener_close_races(&netif, &remote);
    test_pool_reuse();
    return 0;
}
