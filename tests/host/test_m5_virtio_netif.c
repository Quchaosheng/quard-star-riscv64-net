#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <timeros/net/netif.h>
#include <timeros/net/netif_virtio.h>
#include <timeros/net/pktbuf.h>
#include <timeros/virtio_net.h>

static uint8_t rx_frame[ETHERNET_MAX_FRAME];
static uint32_t rx_length;
static int rx_ready;
static uint8_t tx_frame[ETHERNET_MAX_FRAME];
static uint32_t tx_length;
static int tx_calls;

int virtio_net_get_mac(u8 *mac)
{
    static const u8 expected[] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
    memcpy(mac, expected, sizeof(expected));
    return 0;
}

int virtio_net_receive(void *frame, u32 capacity, u32 *length, u64 deadline)
{
    (void)deadline;
    if (!rx_ready || capacity < rx_length)
        return -1;
    memcpy(frame, rx_frame, rx_length);
    *length = rx_length;
    rx_ready = 0;
    return 0;
}

int virtio_net_send(const void *frame, u32 length)
{
    assert(length <= sizeof(tx_frame));
    memcpy(tx_frame, frame, length);
    tx_length = length;
    tx_calls++;
    return 0;
}

static net_err_t link_open(netif_t *netif)
{
    (void)netif;
    return NET_ERR_OK;
}

static void link_close(netif_t *netif)
{
    (void)netif;
}

static const link_layer_t link_layer = {
    .type = NETIF_TYPE_ETHER,
    .open = link_open,
    .close = link_close,
};

static void test_rx(void)
{
    for (uint32_t i = 0; i < 64; i++)
        rx_frame[i] = (uint8_t)(i ^ 0xa5);
    rx_length = 64;
    rx_ready = 1;

    netif_t *netif = netif_get_default();
    assert(netif_virtio_poll(netif, 1234) == NET_ERR_OK);
    pktbuf_t *buf = netif_get_in(netif, -1);
    assert(buf != 0 && (uint32_t)pktbuf_total(buf) == rx_length);
    uint8_t result[64];
    assert(pktbuf_read(buf, result, sizeof(result)) == NET_ERR_OK);
    assert(memcmp(result, rx_frame, sizeof(result)) == 0);
    pktbuf_free(buf);
    assert(netif_virtio_poll(netif, 1234) == NET_ERR_TMO);
}

static void test_tx(void)
{
    netif_t *netif = netif_get_default();
    pktbuf_t *buf = pktbuf_alloc(20);
    assert(buf != 0);
    uint8_t payload[20];
    for (int i = 0; i < (int)sizeof(payload); i++)
        payload[i] = (uint8_t)(0x30 + i);
    assert(pktbuf_write(buf, payload, sizeof(payload)) == NET_ERR_OK);
    assert(netif_put_out(netif, buf, -1) == NET_ERR_OK);
    assert(netif_virtio_xmit(netif) == NET_ERR_OK);
    assert(tx_calls == 1 && tx_length == ETHERNET_MIN_FRAME);
    assert(memcmp(tx_frame, payload, sizeof(payload)) == 0);
    for (uint32_t i = sizeof(payload); i < tx_length; i++)
        assert(tx_frame[i] == 0);
}

int main(void)
{
    assert(pktbuf_init() == NET_ERR_OK);
    assert(netif_init() == NET_ERR_OK);
    assert(netif_register_layer(NETIF_TYPE_ETHER, &link_layer) == NET_ERR_OK);

    netif_t *netif = netif_open("virt0", &netif_virtio_ops, 0);
    assert(netif != 0);
    assert(netif->hwaddr.len == 6);
    assert(netif->mtu == 1500);
    assert(netif_set_active(netif) == NET_ERR_OK);
    netif_set_default(netif);

    test_rx();
    test_tx();

    assert(netif_set_deactive(netif) == NET_ERR_OK);
    assert(netif_close(netif) == NET_ERR_OK);
    return 0;
}
