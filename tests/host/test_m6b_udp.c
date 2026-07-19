#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <sched.h>

#include <timeros/net/net_sys.h>
#include <timeros/net/ether.h>
#include <timeros/net/ipv4.h>
#include <timeros/net/loop.h>
#include <timeros/net/netif.h>
#include <timeros/net/pktbuf.h>
#include <timeros/net/tools.h>
#include <timeros/net/udp.h>

static int close_wait_started;
static int close_wait_result;

static void *close_wait_thread(void *arg)
{
    udp_pcb_t *pcb = arg;
    uint8_t byte;

    __atomic_store_n(&close_wait_started, 1, __ATOMIC_RELEASE);
    close_wait_result = udp_recvfrom(pcb, &byte, 1, 0, 0, 0);
    return 0;
}
static void test_udp_header_validation(void)
{
    udp_hdr_t header = { 0 };

    header.src_port = 1;
    header.dest_port = 2;
    header.total_len = x_htons(8);
    assert(udp_header_check(&header, sizeof(header)) == NET_ERR_OK);
    header.total_len = x_htons(7);
    assert(udp_header_check(&header, sizeof(header)) == NET_ERR_SIZE);
    assert(udp_header_check(&header, sizeof(header) - 1) == NET_ERR_SIZE);
}

static void test_udp_bind_conflict_and_close(void)
{
    udp_pcb_t first;
    udp_pcb_t second;

    assert(net_sys_init() == NET_ERR_OK);
    assert(pktbuf_init() == NET_ERR_OK);
    assert(udp_init() == NET_ERR_OK);
    assert(udp_open(&first) == NET_ERR_OK);
    assert(udp_open(&second) == NET_ERR_OK);
    assert(udp_bind(&first, 4000) == NET_ERR_OK);
    assert(udp_bind(&second, 4000) == NET_ERR_EXIST);
    assert(udp_close(&first) == NET_ERR_OK);
    assert(udp_bind(&second, 4000) == NET_ERR_OK);
    assert(udp_close(&second) == NET_ERR_OK);
}

static void test_udp_loopback_send_receive(void)
{
    static const uint8_t payload[] = "m6b-udp";
    uint8_t received[16];
    ipaddr_t source;
    uint16_t source_port = 0;
    udp_pcb_t sender;
    udp_pcb_t receiver;

    assert(netif_init() == NET_ERR_OK);
    assert(ether_init() == NET_ERR_OK);
    assert(ipv4_init() == NET_ERR_OK);
    assert(loop_init() == NET_ERR_OK);
    assert(udp_init() == NET_ERR_OK);
    assert(udp_open(&sender) == NET_ERR_OK);
    assert(udp_open(&receiver) == NET_ERR_OK);
    assert(udp_bind(&sender, 4100) == NET_ERR_OK);
    assert(udp_bind(&receiver, 4200) == NET_ERR_OK);
    netif_t *loop = loop_get_netif();
    assert(udp_sendto(&sender, loop, &loop->ipaddr, 4200,
                      payload, sizeof(payload) - 1) == NET_ERR_OK);
    pktbuf_t *packet = netif_get_in(loop, -1);
    assert(packet != 0);
    assert(ipv4_in(loop, packet) == NET_ERR_OK);
    assert(udp_recvfrom(&receiver, received, sizeof(received), &source,
                        &source_port, -1) == (int)sizeof(payload) - 1);
    assert(source_port == 4100);
    assert(ipaddr_is_equal(&source, &loop->ipaddr));
    for (int i = 0; i < (int)sizeof(payload) - 1; i++)
        assert(received[i] == payload[i]);
    assert(udp_recvfrom(&receiver, received, sizeof(received), &source,
                        &source_port, -1) == NET_ERR_NONE);
    assert(udp_recvfrom(&receiver, received, sizeof(received), &source,
                        &source_port, 10) == NET_ERR_TMO);
    assert(udp_close(&sender) == NET_ERR_OK);
    assert(udp_close(&receiver) == NET_ERR_OK);
    assert(netif_set_deactive(loop) == NET_ERR_OK);
    assert(netif_close(loop) == NET_ERR_OK);
}

static void test_udp_rejects_bad_checksum(void)
{
    static const uint8_t payload[] = "bad";
    udp_pcb_t sender;
    udp_pcb_t receiver;

    assert(netif_init() == NET_ERR_OK);
    assert(ether_init() == NET_ERR_OK);
    assert(ipv4_init() == NET_ERR_OK);
    assert(loop_init() == NET_ERR_OK);
    assert(udp_init() == NET_ERR_OK);
    assert(udp_open(&sender) == NET_ERR_OK);
    assert(udp_open(&receiver) == NET_ERR_OK);
    assert(udp_bind(&sender, 4300) == NET_ERR_OK);
    assert(udp_bind(&receiver, 4400) == NET_ERR_OK);
    netif_t *loop = loop_get_netif();
    assert(udp_sendto(&sender, loop, &loop->ipaddr, 4400,
                      payload, sizeof(payload) - 1) == NET_ERR_OK);
    pktbuf_t *packet = netif_get_in(loop, -1);
    assert(packet != 0);
    assert(pktbuf_set_cont(packet, IPV4_HEADER_MIN + UDP_HEADER_SIZE) ==
           NET_ERR_OK);
    uint8_t *bytes = pktbuf_data(packet);
    bytes[IPV4_HEADER_MIN + 6] ^= 0x01;
    assert(ipv4_in(loop, packet) == NET_ERR_CHKSUM);
    pktbuf_free(packet);
    assert(udp_close(&sender) == NET_ERR_OK);
    assert(udp_close(&receiver) == NET_ERR_OK);
    assert(netif_set_deactive(loop) == NET_ERR_OK);
    assert(netif_close(loop) == NET_ERR_OK);
}

static void test_udp_close_wakes_receiver(void)
{
    pthread_t thread;
    udp_pcb_t pcb;

    assert(udp_open(&pcb) == NET_ERR_OK);
    assert(udp_bind(&pcb, 4500) == NET_ERR_OK);
    close_wait_started = 0;
    assert(pthread_create(&thread, 0, close_wait_thread, &pcb) == 0);
    while (!__atomic_load_n(&close_wait_started, __ATOMIC_ACQUIRE))
        sched_yield();
    assert(udp_close(&pcb) == NET_ERR_OK);
    assert(pthread_join(thread, 0) == 0);
    assert(close_wait_result == NET_ERR_STATE);
}

static void test_udp_close_wakes_receiver_with_full_queue(void)
{
    pthread_t thread;
    udp_pcb_t sender;
    udp_pcb_t receiver;

    assert(netif_init() == NET_ERR_OK);
    assert(ether_init() == NET_ERR_OK);
    assert(ipv4_init() == NET_ERR_OK);
    assert(loop_init() == NET_ERR_OK);
    assert(udp_init() == NET_ERR_OK);
    assert(udp_open(&sender) == NET_ERR_OK);
    assert(udp_open(&receiver) == NET_ERR_OK);
    assert(udp_bind(&sender, 4510) == NET_ERR_OK);
    assert(udp_bind(&receiver, 4520) == NET_ERR_OK);
    netif_t *loop = loop_get_netif();
    for (int i = 0; i < UDP_RECV_MAX; i++) {
        uint8_t byte = (uint8_t)i;
        assert(udp_sendto(&sender, loop, &loop->ipaddr, 4520,
                          &byte, 1) == NET_ERR_OK);
        pktbuf_t *packet = netif_get_in(loop, -1);
        assert(packet != 0);
        assert(ipv4_in(loop, packet) == NET_ERR_OK);
    }
    close_wait_started = 0;
    assert(pthread_create(&thread, 0, close_wait_thread, &receiver) == 0);
    while (!__atomic_load_n(&close_wait_started, __ATOMIC_ACQUIRE))
        sched_yield();
    assert(udp_close(&receiver) == NET_ERR_OK);
    assert(pthread_join(thread, 0) == 0);
    assert(close_wait_result == NET_ERR_STATE);
    assert(udp_close(&sender) == NET_ERR_OK);
    assert(netif_set_deactive(loop) == NET_ERR_OK);
    assert(netif_close(loop) == NET_ERR_OK);
}

static void test_udp_discards_bytes_after_declared_length(void)
{
    udp_pcb_t receiver;
    ipaddr_t source;
    uint8_t received[4] = { 0 };
    pktbuf_t *packet = pktbuf_alloc(UDP_HEADER_SIZE + 2);

    assert(netif_init() == NET_ERR_OK);
    assert(ether_init() == NET_ERR_OK);
    assert(ipv4_init() == NET_ERR_OK);
    assert(loop_init() == NET_ERR_OK);
    assert(udp_init() == NET_ERR_OK);
    assert(packet != 0);
    udp_hdr_t *header = (udp_hdr_t *)pktbuf_data(packet);
    header->src_port = x_htons(4530);
    header->dest_port = x_htons(4540);
    header->total_len = x_htons(UDP_HEADER_SIZE + 1);
    header->checksum = 0;
    pktbuf_reset_acc(packet);
    assert(pktbuf_seek(packet, UDP_HEADER_SIZE) == NET_ERR_OK);
    static const uint8_t payload[] = { 'A', 'B' };
    assert(pktbuf_write(packet, payload, sizeof(payload)) == NET_ERR_OK);
    assert(udp_open(&receiver) == NET_ERR_OK);
    assert(udp_bind(&receiver, 4540) == NET_ERR_OK);
    ipaddr_from_str(&source, "10.0.0.1");
    assert(udp_in(loop_get_netif(), &source, &loop_get_netif()->ipaddr,
                  packet) == NET_ERR_OK);
    assert(udp_recvfrom(&receiver, received, sizeof(received), 0, 0, -1) == 1);
    assert(received[0] == 'A');
    assert(udp_close(&receiver) == NET_ERR_OK);
    netif_t *loop = loop_get_netif();
    assert(netif_set_deactive(loop) == NET_ERR_OK);
    assert(netif_close(loop) == NET_ERR_OK);
}

int main(void)
{
    test_udp_header_validation();
    test_udp_bind_conflict_and_close();
    test_udp_loopback_send_receive();
    test_udp_rejects_bad_checksum();
    test_udp_close_wakes_receiver();
    test_udp_close_wakes_receiver_with_full_queue();
    test_udp_discards_bytes_after_declared_length();
    return 0;
}
