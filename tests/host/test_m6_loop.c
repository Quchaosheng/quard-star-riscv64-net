#include <assert.h>
#include <stdint.h>

#include <timeros/net/arp.h>
#include <timeros/net/ether.h>
#include <timeros/net/icmpv4.h>
#include <timeros/net/ipv4.h>
#include <timeros/net/loop.h>
#include <timeros/net/net_stack.h>
#include <timeros/net/net_sys.h>
#include <timeros/net/netif.h>
#include <timeros/net/pktbuf.h>
#include <timeros/net/protocol.h>
#include <timeros/net/timer.h>
#include <timeros/net/tools.h>

int virtio_net_get_mac(uint8_t *mac)
{
    (void)mac;
    return -1;
}

int virtio_net_receive(void *frame, uint32_t capacity, uint32_t *length,
                       uint64_t deadline)
{
    (void)frame;
    (void)capacity;
    (void)length;
    (void)deadline;
    return -1;
}

int virtio_net_send(const void *frame, uint32_t length)
{
    (void)frame;
    (void)length;
    return -1;
}

uint64_t net_stack_test_now(void)
{
    return 0;
}

static net_err_t fail_xmit(netif_t *netif)
{
    pktbuf_t *buf = netif_get_out(netif, -1);

    assert(buf != 0);
    assert(buf->ref == 2);
    pktbuf_free(buf);
    return NET_ERR_FULL;
}

static const netif_ops_t fail_xmit_ops = {
    .xmit = fail_xmit,
};

static net_err_t fail_ether_open(netif_t *netif, void *data)
{
    static const uint8_t hwaddr[ETH_HWA_SIZE] = { 1, 2, 3, 4, 5, 6 };

    (void)data;
    netif->type = NETIF_TYPE_ETHER;
    netif->mtu = ETH_MTU;
    return netif_set_hwaddr(netif, hwaddr, sizeof(hwaddr));
}

static const netif_ops_t fail_ether_ops = {
    .open = fail_ether_open,
    .xmit = fail_xmit,
};

static void test_arp_reply_xmit_error(void)
{
    static const uint8_t peer_mac[ETH_HWA_SIZE] = { 6, 5, 4, 3, 2, 1 };
    ipaddr_t ip;
    ipaddr_t mask;

    netif_t *netif = netif_open("fail0", &fail_ether_ops, 0);
    assert(netif != 0);
    assert(ipaddr_from_str(&ip, "10.0.0.1") == NET_ERR_OK);
    assert(ipaddr_from_str(&mask, "255.0.0.0") == NET_ERR_OK);
    assert(netif_set_addr(netif, &ip, &mask, 0) == NET_ERR_OK);
    assert(netif_set_active(netif) == NET_ERR_OK);

    pktbuf_t *buf = pktbuf_alloc(ETH_FRAME_MIN);
    assert(buf != 0);
    assert(pktbuf_fill(buf, 0, ETH_FRAME_MIN) == NET_ERR_OK);
    ether_hdr_t *ether = (ether_hdr_t *)pktbuf_data(buf);
    for (int i = 0; i < ETH_HWA_SIZE; i++) {
        ether->dest[i] = netif->hwaddr.addr[i];
        ether->src[i] = peer_mac[i];
    }
    ether->protocol = x_htons(NET_PROTOCOL_ARP);
    arp_pkt_t *arp = (arp_pkt_t *)(pktbuf_data(buf) + sizeof(*ether));
    arp->htype = x_htons(ARP_HW_ETHER);
    arp->ptype = x_htons(NET_PROTOCOL_IPV4);
    arp->hlen = ETH_HWA_SIZE;
    arp->plen = IPV4_ADDR_SIZE;
    arp->opcode = x_htons(ARP_REQUEST);
    for (int i = 0; i < ETH_HWA_SIZE; i++)
        arp->send_haddr[i] = peer_mac[i];
    arp->send_paddr[0] = 10;
    arp->send_paddr[3] = 2;
    ipaddr_to_buf(&ip, arp->target_paddr);

    assert(netif_put_in(netif, buf, -1) == NET_ERR_OK);
    assert(net_stack_process_input(netif) == NET_ERR_FULL);
    assert(fixq_count(&netif->in_q) == 0);
    assert(fixq_count(&netif->out_q) == 0);
    assert(netif_set_deactive(netif) == NET_ERR_OK);
    assert(netif_close(netif) == NET_ERR_OK);
}

static void assert_pool_fully_reclaimed(void)
{
    pktbuf_t *buffers[PKTBUF_BUF_CNT];

    for (int i = 0; i < PKTBUF_BUF_CNT; i++) {
        buffers[i] = pktbuf_alloc(1);
        assert(buffers[i] != 0);
    }
    assert(pktbuf_alloc(1) == 0);
    for (int i = 0; i < PKTBUF_BUF_CNT; i++)
        pktbuf_free(buffers[i]);
}

int main(void)
{
    static const uint8_t payload[] = { 'l', 'o', 'o', 'p' };
    icmpv4_stats_t stats;

    assert(net_sys_init() == NET_ERR_OK);
    assert(pktbuf_init() == NET_ERR_OK);
    assert(netif_init() == NET_ERR_OK);
    assert(ether_init() == NET_ERR_OK);
    assert(net_timer_init() == NET_ERR_OK);
    assert(arp_init() == NET_ERR_OK);
    assert(ipv4_init() == NET_ERR_OK);
    assert(icmpv4_init() == NET_ERR_OK);

    assert(loop_init() == NET_ERR_OK);
    netif_t *loop = loop_get_netif();
    assert(loop != 0);
    assert(loop->state == NETIF_ACTIVE);
    assert(loop->type == NETIF_TYPE_LOOP);
    assert(loop->ipaddr.q_addr == 0x7f000001U);
    assert(loop->netmask.q_addr == 0xff000000U);
    assert(loop->mtu == 1500);
    assert(loop_init() == NET_ERR_EXIST);

    assert(net_stack_process_input(loop) == NET_ERR_NONE);
    assert(icmpv4_out_echo(loop, &loop->ipaddr, 0x6d36, 1,
                           payload, sizeof(payload)) == NET_ERR_OK);
    icmpv4_get_stats(&stats);
    assert(stats.echo_requests == 0);
    assert(stats.echo_replies == 0);
    assert(fixq_count(&loop->in_q) == 1);
    assert(fixq_count(&loop->out_q) == 0);

    assert(net_stack_process_input(loop) == NET_ERR_OK);
    icmpv4_get_stats(&stats);
    assert(stats.echo_requests == 1);
    assert(stats.echo_replies == 0);
    assert(fixq_count(&loop->in_q) == 1);

    assert(net_stack_process_input(loop) == NET_ERR_OK);
    icmpv4_get_stats(&stats);
    assert(stats.echo_requests == 1);
    assert(stats.echo_replies == 1);
    assert(stats.last_reply_identifier == 0x6d36);
    assert(stats.last_reply_sequence == 1);
    assert(net_stack_process_input(loop) == NET_ERR_NONE);

    assert(icmpv4_out_echo(loop, &loop->ipaddr, 0x6d36, 3,
                           payload, sizeof(payload)) == NET_ERR_OK);
    const netif_ops_t *saved_ops = loop->ops;
    loop->ops = &fail_xmit_ops;
    assert(net_stack_process_input(loop) == NET_ERR_FULL);
    loop->ops = saved_ops;
    assert(fixq_count(&loop->in_q) == 0);
    assert(fixq_count(&loop->out_q) == 0);

    test_arp_reply_xmit_error();

    pktbuf_t *bad = pktbuf_alloc(1);
    assert(bad != 0);
    assert(netif_put_in(loop, bad, -1) == NET_ERR_OK);
    assert(net_stack_process_input(loop) == NET_ERR_SIZE);

    for (int i = 0; i < NETIF_OUTQ_SIZE; i++) {
        pktbuf_t *queued = pktbuf_alloc(1);
        assert(queued != 0);
        assert(netif_put_out(loop, queued, -1) == NET_ERR_OK);
    }
    assert(icmpv4_out_echo(loop, &loop->ipaddr, 0x6d36, 2,
                           payload, sizeof(payload)) == NET_ERR_FULL);
    for (int i = 0; i < NETIF_OUTQ_SIZE; i++)
        pktbuf_free(netif_get_out(loop, -1));

    for (int i = 0; i < NETIF_INQ_SIZE; i++) {
        pktbuf_t *queued = pktbuf_alloc(1);
        assert(queued != 0);
        assert(netif_put_in(loop, queued, -1) == NET_ERR_OK);
    }
    pktbuf_t *dropped = pktbuf_alloc(1);
    assert(dropped != 0);
    assert(netif_put_out(loop, dropped, -1) == NET_ERR_OK);
    assert(loop->ops->xmit(loop) == NET_ERR_FULL);
    assert(fixq_count(&loop->out_q) == 0);
    assert(netif_set_deactive(loop) == NET_ERR_OK);
    assert(netif_set_active(loop) == NET_ERR_OK);

    assert(icmpv4_out_echo(loop, &loop->ipaddr, 0x6d36, 2,
                           payload, sizeof(payload)) == NET_ERR_OK);
    assert(fixq_count(&loop->in_q) == 1);
    assert(netif_set_deactive(loop) == NET_ERR_OK);
    assert(fixq_count(&loop->in_q) == 0);
    assert(fixq_count(&loop->out_q) == 0);
    assert(loop_get_netif() == loop);
    assert(net_stack_process_input(loop) == NET_ERR_STATE);
    assert(netif_close(loop) == NET_ERR_OK);
    assert(loop_get_netif() == 0);

    assert_pool_fully_reclaimed();
    return 0;
}
