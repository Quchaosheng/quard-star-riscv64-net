#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <timeros/net/ipv4.h>
#include <timeros/net/net_sys.h>
#include <timeros/net/protocol.h>
#include <timeros/net/socket.h>
#include <timeros/net/tcp.h>
#include <timeros/net/timer.h>
#include <timeros/net/tools.h>

static uint16_t output_src_port;
static uint32_t output_seq;
static uint8_t output_flags;

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
    assert(netif != 0);
    assert(dest != 0);
    assert(protocol == NET_PROTOCOL_TCP);
    assert(buf != 0);
    assert(pktbuf_set_cont(buf, TCP_HEADER_SIZE) == NET_ERR_OK);
    tcp_hdr_t *header = (tcp_hdr_t *)pktbuf_data(buf);
    output_src_port = x_ntohs(header->src_port);
    output_seq = x_ntohl(header->seq);
    output_flags = header->flags;
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
    return buf;
}

static void input_ok(netif_t *netif, const ipaddr_t *src,
                     const ipaddr_t *dest, pktbuf_t *buf)
{
    assert(tcp_in(netif, src, dest, buf) == NET_ERR_OK);
}

int main(void)
{
    static const uint8_t payload[] = "socket-stream";
    uint8_t received[sizeof(payload)] = { 0 };
    netif_t netif = { 0 };
    ipaddr_t remote;

    assert(net_sys_init() == NET_ERR_OK);
    assert(pktbuf_init() == NET_ERR_OK);
    assert(net_timer_init() == NET_ERR_OK);
    assert(tcp_init() == NET_ERR_OK);
    assert(net_socket_init() == NET_ERR_OK);
    assert(ipaddr_from_str(&netif.ipaddr, "192.0.2.10") == NET_ERR_OK);
    assert(ipaddr_from_str(&remote, "192.0.2.20") == NET_ERR_OK);
    netif.state = NETIF_ACTIVE;

    int handle = net_socket_open(NET_SOCKET_TCP);
    assert(handle >= 0);
    assert(net_socket_bind(handle, 4000) == NET_ERR_PARAM);
    assert(net_socket_sendto(handle, &netif, &remote, 4800,
                             payload, sizeof(payload)) == NET_ERR_PARAM);
    assert(net_socket_recvfrom(handle, received, sizeof(received),
                               0, 0, -1) == NET_ERR_PARAM);

    assert(net_socket_connect_start(handle, &netif, &remote, 4800) ==
           NET_ERR_OK);
    assert(output_flags == TCP_FLAG_SYN);
    uint16_t local_port = output_src_port;
    uint32_t local_syn = output_seq;
    assert(net_socket_wait_connect(handle, -1) == NET_ERR_NONE);
    input_ok(&netif, &remote, &netif.ipaddr,
             make_segment(&remote, &netif.ipaddr, 4800, local_port,
                          7000, local_syn + 1U,
                          TCP_FLAG_SYN | TCP_FLAG_ACK, 0, 0));
    assert(net_socket_wait_connect(handle, -1) == NET_ERR_OK);

    assert(net_socket_send(handle, payload, sizeof(payload)) == NET_ERR_OK);
    assert(output_flags == (TCP_FLAG_PSH | TCP_FLAG_ACK));
    uint32_t data_seq = output_seq;
    input_ok(&netif, &remote, &netif.ipaddr,
             make_segment(&remote, &netif.ipaddr, 4800, local_port,
                          7001, data_seq + sizeof(payload), TCP_FLAG_ACK,
                          payload, sizeof(payload)));
    assert(net_socket_recv(handle, received, sizeof(received), -1) ==
           (int)sizeof(payload));
    assert(memcmp(received, payload, sizeof(payload)) == 0);

    assert(net_socket_close(handle) == NET_ERR_NONE);
    assert(output_flags == (TCP_FLAG_FIN | TCP_FLAG_ACK));
    uint32_t fin_seq = output_seq;
    input_ok(&netif, &remote, &netif.ipaddr,
             make_segment(&remote, &netif.ipaddr, 4800, local_port,
                          7001 + sizeof(payload), fin_seq + 1U,
                          TCP_FLAG_ACK, 0, 0));
    input_ok(&netif, &remote, &netif.ipaddr,
             make_segment(&remote, &netif.ipaddr, 4800, local_port,
                          7001 + sizeof(payload), fin_seq + 1U,
                          TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0));
    assert(net_timer_check_tmo(TCP_TIME_WAIT_MS) == NET_ERR_OK);
    assert(net_socket_wait_close(handle, -1) == NET_ERR_OK);
    assert(net_socket_close(handle) == NET_ERR_OK);
    assert(net_socket_send(handle, payload, sizeof(payload)) ==
           NET_ERR_PARAM);
    assert(net_socket_close(handle) == NET_ERR_PARAM);

    int reused = net_socket_open(NET_SOCKET_TCP);
    assert(reused >= 0 && reused != handle);
    assert(net_socket_close(reused) == NET_ERR_NONE);
    assert(net_socket_wait_close(reused, -1) == NET_ERR_OK);
    assert(net_socket_close(reused) == NET_ERR_OK);
    return 0;
}
