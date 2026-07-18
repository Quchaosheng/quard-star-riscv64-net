#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <timeros/net/ether.h>
#include <timeros/net/netif.h>
#include <timeros/net/pktbuf.h>
#include <timeros/net/tools.h>

static uint8_t tx_frame[ETH_FRAME_MAX];
static int tx_length;
static int tx_calls;
static int input_calls;

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
    tx_length = pktbuf_total(buf);
    assert(tx_length <= (int)sizeof(tx_frame));
    pktbuf_reset_acc(buf);
    assert(pktbuf_read(buf, tx_frame, tx_length) == NET_ERR_OK);
    pktbuf_free(buf);
    tx_calls++;
    return NET_ERR_OK;
}

static const netif_ops_t fake_ops = {
    .open = fake_open,
    .close = fake_close,
    .xmit = fake_xmit,
};

static net_err_t input_handler(netif_t *netif, pktbuf_t *buf)
{
    (void)netif;
    assert(pktbuf_total(buf) == ETH_DATA_MIN);
    input_calls++;
    pktbuf_free(buf);
    return NET_ERR_OK;
}

static void test_tools(void)
{
    uint8_t bytes[] = { 0x00, 0x01, 0xf2, 0x03 };
    assert(x_htons(0x1234) == 0x3412);
    assert(x_ntohs(0x3412) == 0x1234);
    assert(x_htonl(0x12345678U) == 0x78563412U);
    assert(checksum16(0, bytes, sizeof(bytes), 0, 0) == 0xf204);

    uint32_t sum = checksum16(0, bytes, 3, 0, 0);
    assert(checksum16(3, bytes + 3, 1, sum, 0) == 0xf204);
    assert(checksum16(0, bytes, sizeof(bytes), 0, 1) == 0x0dfb);
}

static void test_ether(void)
{
    uint8_t mac[] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
    uint8_t peer[] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x57 };
    netif_t *netif = netif_open("virt0", &fake_ops, 0);
    assert(netif != 0);
    assert(netif_set_hwaddr(netif, mac, sizeof(mac)) == NET_ERR_OK);
    assert(netif_set_active(netif) == NET_ERR_OK);

    pktbuf_t *buf = pktbuf_alloc(10);
    assert(buf != 0);
    uint8_t payload[10];
    for (int i = 0; i < (int)sizeof(payload); i++)
        payload[i] = (uint8_t)(i + 1);
    assert(pktbuf_write(buf, payload, sizeof(payload)) == NET_ERR_OK);
    assert(ether_raw_out(netif, 0x0800, peer, buf) == NET_ERR_OK);
    assert(tx_calls == 1 && tx_length == ETH_FRAME_MIN);
    assert(memcmp(tx_frame, peer, ETH_HWA_SIZE) == 0);
    assert(memcmp(tx_frame + ETH_HWA_SIZE, mac, ETH_HWA_SIZE) == 0);
    assert(tx_frame[12] == 0x08 && tx_frame[13] == 0x00);
    assert(memcmp(tx_frame + sizeof(ether_hdr_t), payload,
                  sizeof(payload)) == 0);
    for (int i = sizeof(ether_hdr_t) + sizeof(payload); i < tx_length; i++)
        assert(tx_frame[i] == 0);

    buf = pktbuf_alloc(10);
    assert(buf != 0);
    assert(pktbuf_write(buf, payload, sizeof(payload)) == NET_ERR_OK);
    assert(ether_raw_out(netif, 0x0800, mac, buf) == NET_ERR_OK);
    assert(tx_calls == 1);
    pktbuf_t *loopback = netif_get_in(netif, -1);
    assert(loopback != 0 && pktbuf_total(loopback) == ETH_FRAME_MIN);
    pktbuf_free(loopback);

    assert(ether_register_handler(0x0800, input_handler) == NET_ERR_OK);
    buf = pktbuf_alloc(ETH_FRAME_MIN);
    assert(buf != 0);
    memcpy(tx_frame, mac, ETH_HWA_SIZE);
    assert(pktbuf_write(buf, tx_frame, tx_length) == NET_ERR_OK);
    assert(ether_in(netif, buf) == NET_ERR_OK);
    assert(input_calls == 1);

    buf = pktbuf_alloc(ETH_FRAME_MIN);
    assert(buf != 0);
    tx_frame[0] ^= 1;
    assert(pktbuf_write(buf, tx_frame, tx_length) == NET_ERR_OK);
    assert(ether_in(netif, buf) == NET_ERR_UNREACH);
    pktbuf_free(buf);
    tx_frame[0] ^= 1;

    buf = pktbuf_alloc(14);
    assert(buf != 0);
    assert(ether_in(netif, buf) == NET_ERR_SIZE);
    pktbuf_free(buf);

    buf = pktbuf_alloc(13);
    assert(buf != 0);
    assert(ether_in(netif, buf) == NET_ERR_SIZE);
    pktbuf_free(buf);

    assert(netif_set_deactive(netif) == NET_ERR_OK);
    assert(netif_close(netif) == NET_ERR_OK);
}

int main(void)
{
    test_tools();
    assert(pktbuf_init() == NET_ERR_OK);
    assert(netif_init() == NET_ERR_OK);
    assert(ether_init() == NET_ERR_OK);
    test_ether();
    assert(ether_broadcast_addr()[0] == 0xff);
    return 0;
}
