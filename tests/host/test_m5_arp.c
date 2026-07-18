#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <timeros/net/arp.h>
#include <timeros/net/ether.h>
#include <timeros/net/ipaddr.h>
#include <timeros/net/netif.h>
#include <timeros/net/pktbuf.h>
#include <timeros/net/protocol.h>
#include <timeros/net/timer.h>
#include <timeros/net/tools.h>

static uint8_t last_frame[ETH_FRAME_MAX];
static int last_length;
static int tx_count;

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
    assert(buf != 0);
    last_length = pktbuf_total(buf);
    assert(last_length <= (int)sizeof(last_frame));
    pktbuf_reset_acc(buf);
    assert(pktbuf_read(buf, last_frame, last_length) == NET_ERR_OK);
    pktbuf_free(buf);
    tx_count++;
    return NET_ERR_OK;
}

static const netif_ops_t fake_ops = {
    .open = fake_open,
    .close = fake_close,
    .xmit = fake_xmit,
};

static void make_arp_frame(uint8_t *frame, const uint8_t *dest,
                           const uint8_t *src, uint16_t opcode,
                           const uint8_t *send_ip, const uint8_t *target_ip,
                           const uint8_t *target_mac)
{
    arp_pkt_t packet;
    memset(frame, 0, ETH_FRAME_MIN);
    memcpy(frame, dest, ETH_HWA_SIZE);
    memcpy(frame + ETH_HWA_SIZE, src, ETH_HWA_SIZE);
    uint16_t protocol = x_htons(NET_PROTOCOL_ARP);
    memcpy(frame + 12, &protocol, sizeof(protocol));
    packet.htype = x_htons(ARP_HW_ETHER);
    packet.ptype = x_htons(NET_PROTOCOL_IPV4);
    packet.hlen = ETH_HWA_SIZE;
    packet.plen = IPV4_ADDR_SIZE;
    packet.opcode = x_htons(opcode);
    memcpy(packet.send_haddr, src, ETH_HWA_SIZE);
    memcpy(packet.send_paddr, send_ip, IPV4_ADDR_SIZE);
    memcpy(packet.target_haddr, target_mac, ETH_HWA_SIZE);
    memcpy(packet.target_paddr, target_ip, IPV4_ADDR_SIZE);
    memcpy(frame + sizeof(ether_hdr_t), &packet, sizeof(packet));
}

static net_err_t inject_frame(netif_t *netif, const uint8_t *frame)
{
    pktbuf_t *buf = pktbuf_alloc(ETH_FRAME_MIN);
    assert(buf != 0);
    assert(pktbuf_write(buf, (uint8_t *)frame, ETH_FRAME_MIN) == NET_ERR_OK);
    return ether_in(netif, buf);
}

static void test_arp(netif_t *netif, const uint8_t *local_mac,
                     const uint8_t *peer_mac)
{
    ipaddr_t local_ip;
    ipaddr_t target_ip;
    ipaddr_t queued_ip;
    ipaddr_t mask;
    assert(ipaddr_from_str(&local_ip, "192.168.100.2") == NET_ERR_OK);
    assert(ipaddr_from_str(&target_ip, "192.168.100.1") == NET_ERR_OK);
    assert(ipaddr_from_str(&queued_ip, "192.168.100.3") == NET_ERR_OK);
    assert(ipaddr_from_str(&mask, "255.255.255.0") == NET_ERR_OK);
    assert(netif_set_addr(netif, &local_ip, &mask, 0) == NET_ERR_OK);

    assert(arp_make_request(netif, &target_ip) == NET_ERR_OK);
    assert(last_length == ETH_FRAME_MIN &&
           memcmp(last_frame, ether_broadcast_addr(), ETH_HWA_SIZE) == 0);
    arp_pkt_t *request = (arp_pkt_t *)(last_frame + sizeof(ether_hdr_t));
    assert(x_ntohs(request->opcode) == ARP_REQUEST);
    assert(memcmp(request->send_haddr, local_mac, ETH_HWA_SIZE) == 0);

    uint8_t frame[ETH_FRAME_MIN];
    uint8_t target_bytes[IPV4_ADDR_SIZE];
    uint8_t local_bytes[IPV4_ADDR_SIZE];
    ipaddr_to_buf(&target_ip, target_bytes);
    ipaddr_to_buf(&local_ip, local_bytes);
    make_arp_frame(frame, local_mac, peer_mac, ARP_REPLY, target_bytes,
                   local_bytes, local_mac);
    assert(inject_frame(netif, frame) == NET_ERR_OK);
    assert(memcmp(arp_find(netif, &target_ip), peer_mac, ETH_HWA_SIZE) == 0);

    pktbuf_t *payload = pktbuf_alloc(4);
    assert(payload != 0);
    uint8_t bytes[] = { 1, 2, 3, 4 };
    assert(pktbuf_write(payload, bytes, sizeof(bytes)) == NET_ERR_OK);
    int sent_before = tx_count;
    assert(arp_resolve(netif, &target_ip, payload) == NET_ERR_OK);
    assert(tx_count == sent_before + 1);
    assert(last_frame[12] == 0x08 && last_frame[13] == 0x00);

    payload = pktbuf_alloc(4);
    assert(payload != 0);
    assert(pktbuf_write(payload, bytes, sizeof(bytes)) == NET_ERR_OK);
    sent_before = tx_count;
    assert(arp_resolve(netif, &queued_ip, payload) == NET_ERR_OK);
    assert(tx_count == sent_before + 1);
    arp_pkt_t *queued_request =
        (arp_pkt_t *)(last_frame + sizeof(ether_hdr_t));
    assert(x_ntohs(queued_request->opcode) == ARP_REQUEST);
    uint8_t queued_bytes[IPV4_ADDR_SIZE];
    ipaddr_to_buf(&queued_ip, queued_bytes);
    make_arp_frame(frame, local_mac, peer_mac, ARP_REPLY, queued_bytes,
                   local_bytes, local_mac);
    sent_before = tx_count;
    assert(inject_frame(netif, frame) == NET_ERR_OK);
    assert(tx_count == sent_before + 1);
    assert(last_frame[12] == 0x08 && last_frame[13] == 0x00);
    assert(memcmp(last_frame + sizeof(ether_hdr_t) + 0, bytes,
                  sizeof(bytes)) == 0);

    uint8_t request_ip[] = { 192, 168, 100, 4 };
    make_arp_frame(frame, ether_broadcast_addr(), peer_mac, ARP_REQUEST,
                   request_ip, local_bytes, ether_broadcast_addr());
    sent_before = tx_count;
    assert(inject_frame(netif, frame) == NET_ERR_OK);
    assert(tx_count == sent_before + 1);
    arp_pkt_t *reply = (arp_pkt_t *)(last_frame + sizeof(ether_hdr_t));
    assert(x_ntohs(reply->opcode) == ARP_REPLY);
    assert(memcmp(reply->target_haddr, peer_mac, ETH_HWA_SIZE) == 0);
}

int main(void)
{
    static const uint8_t local_mac[] = {
        0x52, 0x54, 0x00, 0x12, 0x34, 0x56,
    };
    static const uint8_t peer_mac[] = {
        0x52, 0x54, 0x00, 0x12, 0x34, 0x57,
    };

    assert(pktbuf_init() == NET_ERR_OK);
    assert(net_timer_init() == NET_ERR_OK);
    assert(netif_init() == NET_ERR_OK);
    assert(ether_init() == NET_ERR_OK);
    assert(arp_init() == NET_ERR_OK);
    netif_t *netif = netif_open("virt0", &fake_ops, 0);
    assert(netif != 0);
    assert(netif_set_hwaddr(netif, local_mac, sizeof(local_mac)) == NET_ERR_OK);
    assert(netif_set_active(netif) == NET_ERR_OK);
    test_arp(netif, local_mac, peer_mac);
    assert(arp_find(netif, &(ipaddr_t){ .q_addr = 0 }) == 0);
    assert(netif_set_deactive(netif) == NET_ERR_OK);
    assert(netif_close(netif) == NET_ERR_OK);
    return 0;
}
