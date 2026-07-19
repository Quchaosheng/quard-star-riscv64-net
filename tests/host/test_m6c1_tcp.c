#include <assert.h>
#include <stdint.h>

#include <timeros/net/ipaddr.h>
#include <timeros/net/net_sys.h>
#include <timeros/net/pktbuf.h>
#include <timeros/net/tcp.h>
#include <timeros/net/tools.h>

static void test_tcp_header_validation(void)
{
    tcp_hdr_t header = { 0 };

    header.data_offset = 5U << 4;
    assert(tcp_header_check(&header, TCP_HEADER_SIZE - 1) == NET_ERR_SIZE);
    header.data_offset = 4U << 4;
    assert(tcp_header_check(&header, TCP_HEADER_SIZE) == NET_ERR_SIZE);
    header.data_offset = (5U << 4) | 1U;
    assert(tcp_header_check(&header, TCP_HEADER_SIZE) == NET_ERR_FORMAT);
    header.data_offset = 5U << 4;
    assert(tcp_header_check(&header, TCP_HEADER_SIZE) == NET_ERR_OK);
}

static void test_tcp_ack_acceptance(void)
{
    assert(tcp_state_accept_ack(TCP_STATE_SYN_SENT, 101, 100) == NET_ERR_OK);
    assert(tcp_state_accept_ack(TCP_STATE_SYN_SENT, 100, 100) == NET_ERR_STATE);
}

static void test_tcp_sequence_window(void)
{
    assert(tcp_sequence_in_window(100, 100, 32));
    assert(!tcp_sequence_in_window(132, 100, 32));
}

static void test_tcp_checksum(void)
{
    ipaddr_t src;
    ipaddr_t dest;
    pktbuf_t *packet = pktbuf_alloc(TCP_HEADER_SIZE);

    assert(packet != 0);
    assert(ipaddr_from_str(&src, "192.0.2.1") == NET_ERR_OK);
    assert(ipaddr_from_str(&dest, "198.51.100.2") == NET_ERR_OK);
    tcp_hdr_t *header = (tcp_hdr_t *)pktbuf_data(packet);
    header->src_port = x_htons(1000);
    header->dest_port = x_htons(2000);
    header->seq = x_htonl(1);
    header->ack = 0;
    header->data_offset = 5U << 4;
    header->flags = 0x02;
    header->window = x_htons(4096);
    header->checksum = 0;
    header->urgent = 0;
    assert(tcp_checksum(packet, &src, &dest, TCP_HEADER_SIZE) == 0xa7f2);
    pktbuf_free(packet);
}

int main(void)
{
    assert(net_sys_init() == NET_ERR_OK);
    assert(pktbuf_init() == NET_ERR_OK);
    test_tcp_header_validation();
    test_tcp_ack_acceptance();
    test_tcp_sequence_window();
    test_tcp_checksum();
    return 0;
}
