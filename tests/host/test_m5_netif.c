#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <timeros/net/ipaddr.h>
#include <timeros/net/netif.h>
#include <timeros/net/pktbuf.h>

static int driver_open_count;
static int driver_close_count;
static int driver_xmit_count;

static net_err_t fake_open(netif_t *netif, void *data)
{
    assert(data != 0);
    netif->type = NETIF_TYPE_ETHER;
    netif->mtu = 1500;
    driver_open_count++;
    return NET_ERR_OK;
}

static void fake_close(netif_t *netif)
{
    (void)netif;
    driver_close_count++;
}

static net_err_t fake_xmit(netif_t *netif)
{
    (void)netif;
    driver_xmit_count++;
    return NET_ERR_OK;
}

static net_err_t fake_link_open(netif_t *netif)
{
    (void)netif;
    return NET_ERR_OK;
}

static void fake_link_close(netif_t *netif)
{
    (void)netif;
}

static net_err_t fake_link_out(netif_t *netif, ipaddr_t *dest, pktbuf_t *buf)
{
    (void)dest;
    net_err_t err = netif_put_out(netif, buf, -1);
    if (err < 0)
        return err;
    return netif->ops->xmit(netif);
}

static const link_layer_t fake_link = {
    .type = NETIF_TYPE_ETHER,
    .open = fake_link_open,
    .close = fake_link_close,
    .out = fake_link_out,
};

static const netif_ops_t fake_ops = {
    .open = fake_open,
    .close = fake_close,
    .xmit = fake_xmit,
};

static void test_ipaddr(void)
{
    ipaddr_t addr;
    uint8_t bytes[IPV4_ADDR_SIZE];
    const char *invalid[] = {
        "", "1.2.3", "1.2.3.4.5", "256.1.1.1", "1..2.3",
        "1.2.3.", "1.2.3.4x", " 1.2.3.4", "1.2.3.04",
    };

    assert(ipaddr_from_str(&addr, "192.168.100.2") == NET_ERR_OK);
    ipaddr_to_buf(&addr, bytes);
    assert(bytes[0] == 192 && bytes[1] == 168 && bytes[2] == 100 &&
           bytes[3] == 2);

    uint32_t original = addr.q_addr;
    for (unsigned int i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        assert(ipaddr_from_str(&addr, invalid[i]) == NET_ERR_PARAM);
        assert(addr.q_addr == original);
    }

    ipaddr_t mask;
    ipaddr_t same;
    ipaddr_t peer;
    ipaddr_t outside;
    ipaddr_t broadcast;
    assert(ipaddr_from_str(&mask, "255.255.255.0") == NET_ERR_OK);
    assert(ipaddr_from_str(&same, "192.168.100.2") == NET_ERR_OK);
    assert(ipaddr_from_str(&peer, "192.168.100.1") == NET_ERR_OK);
    assert(ipaddr_from_str(&outside, "192.168.101.1") == NET_ERR_OK);
    assert(ipaddr_from_str(&broadcast, "192.168.100.255") == NET_ERR_OK);
    assert(ipaddr_is_match(&addr, &same, &mask));
    assert(!ipaddr_is_match(&addr, &peer, &mask));
    assert(!ipaddr_is_match(&addr, &outside, &mask));
    assert(ipaddr_is_direct_broadcast(&broadcast, &mask));
    assert(ipaddr_1_cnt(&mask) == 24);

    ipaddr_set_all_1(&broadcast);
    assert(ipaddr_is_local_broadcast(&broadcast));
    ipaddr_set_loop(&broadcast);
    assert(ipaddr_from_str(&addr, "127.0.0.1") == NET_ERR_OK);
    assert(ipaddr_is_equal(&broadcast, &addr));
}

static void test_netif(void)
{
    int driver_data;
    uint8_t mac[] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
    ipaddr_t addr;
    ipaddr_t mask;
    ipaddr_t gateway;

    assert(pktbuf_init() == NET_ERR_OK);
    assert(netif_init() == NET_ERR_OK);
    assert(netif_register_layer(-1, 0) == NET_ERR_PARAM);
    assert(netif_register_layer(NETIF_TYPE_ETHER, &fake_link) == NET_ERR_OK);

    netif_t *netif = netif_open("virt0", &fake_ops, &driver_data);
    assert(netif != 0);
    assert(netif->state == NETIF_OPENED);
    assert(netif->type == NETIF_TYPE_ETHER);
    assert(driver_open_count == 1);
    assert(netif_set_hwaddr(netif, mac, sizeof(mac)) == NET_ERR_OK);
    assert(netif_set_hwaddr(netif, mac, NETIF_HWADDR_SIZE + 1) == NET_ERR_SIZE);
    assert(ipaddr_from_str(&addr, "192.168.100.2") == NET_ERR_OK);
    assert(ipaddr_from_str(&mask, "255.255.255.0") == NET_ERR_OK);
    assert(ipaddr_from_str(&gateway, "192.168.100.1") == NET_ERR_OK);
    assert(netif_set_addr(netif, &addr, &mask, &gateway) == NET_ERR_OK);
    assert(netif_set_active(netif) == NET_ERR_OK);
    assert(netif->state == NETIF_ACTIVE);
    assert(netif_set_active(netif) == NET_ERR_STATE);

    netif_set_default(netif);
    assert(netif_get_default() == netif);

    pktbuf_t *buf = pktbuf_alloc(64);
    assert(buf != 0);
    assert(netif_put_in(netif, buf, -1) == NET_ERR_OK);
    assert(netif_get_in(netif, -1) == buf);
    pktbuf_free(buf);

    buf = pktbuf_alloc(64);
    assert(buf != 0);
    assert(netif_out(netif, 0, buf) == NET_ERR_OK);
    assert(driver_xmit_count == 1);
    assert(netif_get_out(netif, -1) == buf);
    pktbuf_free(buf);

    pktbuf_t *held[NETIF_INQ_SIZE + 1];
    for (int i = 0; i < NETIF_INQ_SIZE; i++) {
        held[i] = pktbuf_alloc(1);
        assert(held[i] != 0);
        assert(netif_put_in(netif, held[i], -1) == NET_ERR_OK);
    }
    held[NETIF_INQ_SIZE] = pktbuf_alloc(1);
    assert(held[NETIF_INQ_SIZE] != 0);
    assert(netif_put_in(netif, held[NETIF_INQ_SIZE], -1) == NET_ERR_FULL);
    pktbuf_free(held[NETIF_INQ_SIZE]);

    assert(netif_set_deactive(netif) == NET_ERR_OK);
    assert(netif->state == NETIF_OPENED);
    assert(netif_set_deactive(netif) == NET_ERR_STATE);
    assert(netif_close(netif) == NET_ERR_OK);
    assert(netif_get_default() == 0);
    assert(driver_close_count == 1);
}

int main(void)
{
    test_ipaddr();
    test_netif();
    return 0;
}
