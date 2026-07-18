#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <timeros/net/arp.h>
#include <timeros/net/ether.h>
#include <timeros/net/icmpv4.h>
#include <timeros/net/ipv4.h>
#include <timeros/net/netif.h>
#include <timeros/net/pktbuf.h>
#include <timeros/net/protocol.h>
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

static void inject(netif_t *netif, const uint8_t *frame)
{
    pktbuf_t *buf = pktbuf_alloc(ETH_FRAME_MIN);
    assert(buf != 0);
    assert(pktbuf_write(buf, (uint8_t *)frame, ETH_FRAME_MIN) == NET_ERR_OK);
    assert(ether_in(netif, buf) == NET_ERR_OK);
}

static void make_arp_reply(uint8_t *frame, const uint8_t *local_mac,
                           const uint8_t *peer_mac, const uint8_t *local_ip,
                           const uint8_t *peer_ip)
{
    memset(frame, 0, ETH_FRAME_MIN);
    memcpy(frame, local_mac, ETH_HWA_SIZE);
    memcpy(frame + ETH_HWA_SIZE, peer_mac, ETH_HWA_SIZE);
    uint16_t eth_type = x_htons(NET_PROTOCOL_ARP);
    memcpy(frame + 12, &eth_type, sizeof(eth_type));
    arp_pkt_t packet = {
        .htype = 0,
        .ptype = 0,
        .hlen = ETH_HWA_SIZE,
        .plen = IPV4_ADDR_SIZE,
        .opcode = x_htons(ARP_REPLY),
    };
    packet.htype = x_htons(ARP_HW_ETHER);
    packet.ptype = x_htons(NET_PROTOCOL_IPV4);
    memcpy(packet.send_haddr, peer_mac, ETH_HWA_SIZE);
    memcpy(packet.send_paddr, peer_ip, IPV4_ADDR_SIZE);
    memcpy(packet.target_haddr, local_mac, ETH_HWA_SIZE);
    memcpy(packet.target_paddr, local_ip, IPV4_ADDR_SIZE);
    memcpy(frame + sizeof(ether_hdr_t), &packet, sizeof(packet));
}

static int make_ipv4_icmp(uint8_t *frame, const uint8_t *local_mac,
                          const uint8_t *peer_mac, const uint8_t *local_ip,
                          const uint8_t *peer_ip, uint8_t type,
                          int corrupt_ip, int corrupt_icmp)
{
    const uint8_t payload[] = { 0xde, 0xad, 0xbe, 0xef };
    const int icmp_len = (int)sizeof(icmpv4_hdr_t) + (int)sizeof(payload);
    const int ip_len = (int)sizeof(ipv4_hdr_t) + icmp_len;
    memset(frame, 0, ETH_FRAME_MIN);
    memcpy(frame, local_mac, ETH_HWA_SIZE);
    memcpy(frame + ETH_HWA_SIZE, peer_mac, ETH_HWA_SIZE);
    uint16_t eth_type = x_htons(NET_PROTOCOL_IPV4);
    memcpy(frame + 12, &eth_type, sizeof(eth_type));

    ipv4_hdr_t ip = {
        .version_ihl = 0x45,
        .dscp = 0,
        .total_len = x_htons((uint16_t)ip_len),
        .id = x_htons(7),
        .frag_off = 0,
        .ttl = 64,
        .protocol = NET_PROTOCOL_ICMPV4,
        .hdr_checksum = 0,
    };
    memcpy(ip.src_ip, peer_ip, IPV4_ADDR_SIZE);
    memcpy(ip.dest_ip, local_ip, IPV4_ADDR_SIZE);
    ip.hdr_checksum = x_htons(checksum16(0, &ip, sizeof(ip), 0, 1));
    if (corrupt_ip)
        ip.hdr_checksum ^= x_htons(1);
    memcpy(frame + sizeof(ether_hdr_t), &ip, sizeof(ip));

    icmpv4_hdr_t icmp = {
        .type = type,
        .code = 0,
        .checksum = 0,
        .identifier = x_htons(0x1234),
        .sequence = x_htons(9),
    };
    memcpy(frame + sizeof(ether_hdr_t) + sizeof(ip), &icmp,
           sizeof(icmp));
    memcpy(frame + sizeof(ether_hdr_t) + sizeof(ip) + sizeof(icmp), payload,
           sizeof(payload));
    uint8_t *icmp_bytes = frame + sizeof(ether_hdr_t) + sizeof(ip);
    uint16_t checksum = checksum16(0, icmp_bytes, (u16)icmp_len, 0, 1);
    checksum = x_htons(checksum);
    memcpy(icmp_bytes + 2, &checksum, sizeof(checksum));
    if (corrupt_icmp)
        icmp_bytes[sizeof(icmp)] ^= 1;
    return ip_len;
}

int main(void)
{
    static const uint8_t local_mac[] = {
        0x52, 0x54, 0x00, 0x12, 0x34, 0x56,
    };
    static const uint8_t peer_mac[] = {
        0x52, 0x54, 0x00, 0x12, 0x34, 0x57,
    };
    static const uint8_t local_ip[] = { 192, 168, 100, 2 };
    static const uint8_t peer_ip[] = { 192, 168, 100, 1 };
    uint8_t frame[ETH_FRAME_MIN];

    assert(pktbuf_init() == NET_ERR_OK);
    assert(netif_init() == NET_ERR_OK);
    assert(ether_init() == NET_ERR_OK);
    assert(arp_init() == NET_ERR_OK);
    assert(ipv4_init() == NET_ERR_OK);
    assert(icmpv4_init() == NET_ERR_OK);

    netif_t *netif = netif_open("virt0", &fake_ops, 0);
    assert(netif != 0);
    assert(netif_set_hwaddr(netif, local_mac, sizeof(local_mac)) == NET_ERR_OK);
    ipaddr_t local;
    ipaddr_t mask;
    assert(ipaddr_from_str(&local, "192.168.100.2") == NET_ERR_OK);
    assert(ipaddr_from_str(&mask, "255.255.255.0") == NET_ERR_OK);
    assert(netif_set_addr(netif, &local, &mask, 0) == NET_ERR_OK);
    assert(netif_set_active(netif) == NET_ERR_OK);

    make_arp_reply(frame, local_mac, peer_mac, local_ip, peer_ip);
    inject(netif, frame);
    ipaddr_t peer;
    assert(ipaddr_from_str(&peer, "192.168.100.1") == NET_ERR_OK);
    assert(arp_find(netif, &peer) != 0);

    int request_len = make_ipv4_icmp(frame, local_mac, peer_mac, local_ip,
                                     peer_ip, ICMPV4_ECHO_REQUEST, 0, 0);
    (void)request_len;
    int sent_before = tx_count;
    inject(netif, frame);
    assert(tx_count == sent_before + 1);
    assert(last_frame[12] == 0x08 && last_frame[13] == 0x00);
    ipv4_hdr_t *reply_ip =
        (ipv4_hdr_t *)(last_frame + sizeof(ether_hdr_t));
    assert(memcmp(reply_ip->src_ip, local_ip, IPV4_ADDR_SIZE) == 0);
    assert(memcmp(reply_ip->dest_ip, peer_ip, IPV4_ADDR_SIZE) == 0);
    assert(reply_ip->protocol == NET_PROTOCOL_ICMPV4);
    assert(checksum16(0, reply_ip, sizeof(*reply_ip), 0, 1) == 0);
    icmpv4_hdr_t *reply_icmp =
        (icmpv4_hdr_t *)(last_frame + sizeof(ether_hdr_t) + sizeof(*reply_ip));
    assert(reply_icmp->type == ICMPV4_ECHO_REPLY);
    assert(checksum16(0, reply_icmp, 12, 0, 1) == 0);

    make_ipv4_icmp(frame, local_mac, peer_mac, local_ip, peer_ip,
                   ICMPV4_ECHO_REQUEST, 0, 1);
    pktbuf_t *bad = pktbuf_alloc(ETH_FRAME_MIN);
    assert(bad != 0);
    assert(pktbuf_write(bad, frame, ETH_FRAME_MIN) == NET_ERR_OK);
    assert(ether_in(netif, bad) == NET_ERR_CHKSUM);
    pktbuf_free(bad);

    uint8_t payload[] = { 1, 2, 3, 4 };
    sent_before = tx_count;
    assert(icmpv4_out_echo(netif, &peer, 0x4321, 4, payload,
                           sizeof(payload)) == NET_ERR_OK);
    assert(tx_count == sent_before + 1);
    icmpv4_hdr_t *out_icmp =
        (icmpv4_hdr_t *)(last_frame + sizeof(ether_hdr_t) + sizeof(ipv4_hdr_t));
    assert(out_icmp->type == ICMPV4_ECHO_REQUEST);
    assert(x_ntohs(out_icmp->identifier) == 0x4321);
    assert(x_ntohs(out_icmp->sequence) == 4);

    make_ipv4_icmp(frame, local_mac, peer_mac, local_ip, peer_ip,
                   ICMPV4_ECHO_REQUEST, 1, 0);
    bad = pktbuf_alloc(ETH_FRAME_MIN);
    assert(bad != 0);
    assert(pktbuf_write(bad, frame, ETH_FRAME_MIN) == NET_ERR_OK);
    assert(ether_in(netif, bad) == NET_ERR_CHKSUM);
    pktbuf_free(bad);

    assert(netif_set_deactive(netif) == NET_ERR_OK);
    assert(netif_close(netif) == NET_ERR_OK);
    return 0;
}
