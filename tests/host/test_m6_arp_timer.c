#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <timeros/net/arp.h>
#include <timeros/net/ether.h>
#include <timeros/net/ipaddr.h>
#include <timeros/net/net_cfg.h>
#include <timeros/net/netif.h>
#include <timeros/net/pktbuf.h>
#include <timeros/net/protocol.h>
#include <timeros/net/timer.h>
#include <timeros/net/tools.h>

static const uint8_t local_mac[ETH_HWA_SIZE] = {
    0x52, 0x54, 0x00, 0x12, 0x34, 0x56,
};
static const uint8_t peer_mac[ETH_HWA_SIZE] = {
    0x52, 0x54, 0x00, 0x12, 0x34, 0x57,
};

static int arp_request_count;
static int ipv4_count;
static int fail_next_xmit;

static net_err_t fake_open(netif_t *netif, void *data)
{
    (void)data;
    netif->type = NETIF_TYPE_ETHER;
    netif->mtu = ETH_MTU;
    return NET_ERR_OK;
}

static void fake_close(netif_t *netif)
{
    (void)netif;
}

static net_err_t fake_xmit(netif_t *netif)
{
    pktbuf_t *buf = netif_get_out(netif, -1);
    uint8_t protocol[2];
    net_err_t result = fail_next_xmit ? NET_ERR_IO : NET_ERR_OK;

    assert(buf != 0);
    assert(pktbuf_total(buf) >= (int)sizeof(ether_hdr_t));
    pktbuf_reset_acc(buf);
    assert(pktbuf_seek(buf, 12) == NET_ERR_OK);
    assert(pktbuf_read(buf, protocol, sizeof(protocol)) == NET_ERR_OK);
    if (protocol[0] == 0x08 && protocol[1] == 0x06)
        arp_request_count++;
    else if (protocol[0] == 0x08 && protocol[1] == 0x00)
        ipv4_count++;
    else
        assert(0);
    pktbuf_free(buf);
    fail_next_xmit = 0;
    return result;
}

static const netif_ops_t fake_ops = {
    .open = fake_open,
    .close = fake_close,
    .xmit = fake_xmit,
};

static net_err_t conflicting_arp_handler(netif_t *netif, pktbuf_t *buf)
{
    (void)netif;
    pktbuf_free(buf);
    return NET_ERR_OK;
}

static void reset_tx_counts(void)
{
    arp_request_count = 0;
    ipv4_count = 0;
    fail_next_xmit = 0;
}

static pktbuf_t *make_payload(void)
{
    static const uint8_t payload[] = { 1, 2, 3, 4 };
    pktbuf_t *buf = pktbuf_alloc(sizeof(payload));

    assert(buf != 0);
    assert(pktbuf_write(buf, payload, sizeof(payload)) == NET_ERR_OK);
    return buf;
}

static void inject_reply(netif_t *netif, const ipaddr_t *peer)
{
    pktbuf_t *buf = pktbuf_alloc(sizeof(arp_pkt_t));
    arp_pkt_t packet = {
        .htype = x_htons(ARP_HW_ETHER),
        .ptype = x_htons(NET_PROTOCOL_IPV4),
        .hlen = ETH_HWA_SIZE,
        .plen = IPV4_ADDR_SIZE,
        .opcode = x_htons(ARP_REPLY),
    };

    assert(buf != 0);
    memcpy(packet.send_haddr, peer_mac, ETH_HWA_SIZE);
    ipaddr_to_buf(peer, packet.send_paddr);
    memcpy(packet.target_haddr, local_mac, ETH_HWA_SIZE);
    ipaddr_to_buf(&netif->ipaddr, packet.target_paddr);
    assert(pktbuf_write(buf, (const uint8_t *)&packet, sizeof(packet)) ==
           NET_ERR_OK);
    assert(arp_in(netif, buf) == NET_ERR_OK);
}

static void assert_pktbuf_pool_complete(void)
{
    pktbuf_t *bufs[PKTBUF_BUF_CNT];

    for (int i = 0; i < PKTBUF_BUF_CNT; i++) {
        bufs[i] = pktbuf_alloc(1);
        assert(bufs[i] != 0);
    }
    assert(pktbuf_alloc(1) == 0);
    for (int i = 0; i < PKTBUF_BUF_CNT; i++)
        pktbuf_free(bufs[i]);
}

static int scan_count(int seconds)
{
    return 1 + (seconds - 1) / ARP_TIMER_TMO;
}

static void tick_scans(int scans)
{
    for (int i = 0; i < scans; i++)
        assert(net_timer_check_tmo(ARP_TIMER_TMO * 1000) == NET_ERR_OK);
}

static void test_reinit_preserves_live_cache(netif_t *netif)
{
    ipaddr_t peer;

    assert(ipaddr_from_str(&peer, "192.168.100.9") == NET_ERR_OK);
    reset_tx_counts();
    assert(arp_resolve(netif, &peer, make_payload()) == NET_ERR_OK);
    assert(arp_request_count == 1);
    assert(arp_init() == NET_ERR_EXIST);
    tick_scans(scan_count(ARP_ENTRY_PENDING_TMO));
    assert(arp_request_count == 2);
    inject_reply(netif, &peer);
    assert(ipv4_count == 1);
    assert(memcmp(arp_find(netif, &peer), peer_mac, ETH_HWA_SIZE) == 0);
    arp_clear(netif);
    assert_pktbuf_pool_complete();
}

static void test_unresolved_retries_then_releases(netif_t *netif)
{
    ipaddr_t missing;

    assert(ipaddr_from_str(&missing, "192.168.100.10") == NET_ERR_OK);
    reset_tx_counts();
    assert(arp_resolve(netif, &missing, make_payload()) == NET_ERR_OK);
    assert(arp_request_count == 1);
    tick_scans(scan_count(ARP_ENTRY_PENDING_TMO) * ARP_ENTRY_RETRY_CNT);
    assert(arp_request_count == ARP_ENTRY_RETRY_CNT);
    assert(arp_find(netif, &missing) == 0);
    assert(arp_resolve(netif, &missing, make_payload()) == NET_ERR_OK);
    assert(arp_request_count == ARP_ENTRY_RETRY_CNT + 1);
    arp_clear(netif);
    assert_pktbuf_pool_complete();
}

static void test_reply_flushes_once_and_refreshes(netif_t *netif)
{
    ipaddr_t peer;

    assert(ipaddr_from_str(&peer, "192.168.100.11") == NET_ERR_OK);
    reset_tx_counts();
    assert(arp_resolve(netif, &peer, make_payload()) == NET_ERR_OK);
    tick_scans(scan_count(ARP_ENTRY_PENDING_TMO));
    assert(arp_request_count == 2);
    inject_reply(netif, &peer);
    assert(ipv4_count == 1);
    assert(memcmp(arp_find(netif, &peer), peer_mac, ETH_HWA_SIZE) == 0);
    tick_scans(scan_count(ARP_ENTRY_STABLE_TMO) - 1);
    assert(ipv4_count == 1);
    assert(memcmp(arp_find(netif, &peer), peer_mac, ETH_HWA_SIZE) == 0);
    arp_clear(netif);
    assert_pktbuf_pool_complete();
}

static void test_resolved_entry_ages_and_revalidates(netif_t *netif)
{
    ipaddr_t peer;

    assert(ipaddr_from_str(&peer, "192.168.100.12") == NET_ERR_OK);
    reset_tx_counts();
    inject_reply(netif, &peer);
    assert(memcmp(arp_find(netif, &peer), peer_mac, ETH_HWA_SIZE) == 0);
    tick_scans(scan_count(ARP_ENTRY_STABLE_TMO));
    assert(arp_request_count == 1);
    assert(arp_find(netif, &peer) == 0);
    inject_reply(netif, &peer);
    assert(memcmp(arp_find(netif, &peer), peer_mac, ETH_HWA_SIZE) == 0);
    tick_scans(scan_count(ARP_ENTRY_STABLE_TMO) - 1);
    assert(memcmp(arp_find(netif, &peer), peer_mac, ETH_HWA_SIZE) == 0);
    tick_scans(1);
    assert(arp_find(netif, &peer) == 0);
    tick_scans(scan_count(ARP_ENTRY_PENDING_TMO) * ARP_ENTRY_RETRY_CNT);
    assert(arp_request_count == ARP_ENTRY_RETRY_CNT + 1);
    assert_pktbuf_pool_complete();
}

static void test_initial_request_failure_releases_all(netif_t *netif)
{
    ipaddr_t missing;

    assert(ipaddr_from_str(&missing, "192.168.100.13") == NET_ERR_OK);
    reset_tx_counts();
    fail_next_xmit = 1;
    assert(arp_resolve(netif, &missing, make_payload()) == NET_ERR_IO);
    assert(arp_request_count == 1);
    assert(arp_find(netif, &missing) == 0);
    assert(arp_resolve(netif, &missing, make_payload()) == NET_ERR_OK);
    assert(arp_request_count == 2);
    arp_clear(netif);
    assert_pktbuf_pool_complete();
}

static void test_clear_releases_waiting_and_resolved(netif_t *netif)
{
    ipaddr_t waiting;
    ipaddr_t resolved;

    assert(ipaddr_from_str(&waiting, "192.168.100.14") == NET_ERR_OK);
    assert(ipaddr_from_str(&resolved, "192.168.100.15") == NET_ERR_OK);
    reset_tx_counts();
    assert(arp_resolve(netif, &waiting, make_payload()) == NET_ERR_OK);
    inject_reply(netif, &resolved);
    arp_clear(netif);
    assert(arp_find(netif, &waiting) == 0);
    assert(arp_find(netif, &resolved) == 0);
    int requests_before = arp_request_count;
    tick_scans(scan_count(ARP_ENTRY_STABLE_TMO) +
               scan_count(ARP_ENTRY_PENDING_TMO) * ARP_ENTRY_RETRY_CNT + 1);
    assert(arp_request_count == requests_before);
    assert_pktbuf_pool_complete();
}

int main(void)
{
    ipaddr_t local;
    ipaddr_t mask;

    assert(pktbuf_init() == NET_ERR_OK);
    assert(net_timer_init() == NET_ERR_OK);
    assert(netif_init() == NET_ERR_OK);
    assert(ether_init() == NET_ERR_OK);
    assert(ether_register_handler(NET_PROTOCOL_ARP,
                                  conflicting_arp_handler) == NET_ERR_OK);
    assert(arp_init() == NET_ERR_EXIST);
    assert(net_timer_first_tmo() == 0);

    assert(net_timer_init() == NET_ERR_OK);
    assert(netif_init() == NET_ERR_OK);
    assert(ether_init() == NET_ERR_OK);
    assert(arp_init() == NET_ERR_OK);

    netif_t *netif = netif_open("arp0", &fake_ops, 0);
    assert(netif != 0);
    assert(netif_set_hwaddr(netif, local_mac, sizeof(local_mac)) == NET_ERR_OK);
    assert(ipaddr_from_str(&local, "192.168.100.2") == NET_ERR_OK);
    assert(ipaddr_from_str(&mask, "255.255.255.0") == NET_ERR_OK);
    assert(netif_set_addr(netif, &local, &mask, 0) == NET_ERR_OK);
    assert(netif_set_active(netif) == NET_ERR_OK);

    test_reinit_preserves_live_cache(netif);
    test_unresolved_retries_then_releases(netif);
    test_reply_flushes_once_and_refreshes(netif);
    test_resolved_entry_ages_and_revalidates(netif);
    test_initial_request_failure_releases_all(netif);
    test_clear_releases_waiting_and_resolved(netif);

    assert(netif_set_deactive(netif) == NET_ERR_OK);
    assert(netif_close(netif) == NET_ERR_OK);
    return 0;
}
