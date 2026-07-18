#include <assert.h>
#include <stdint.h>

#include <timeros/net/net_stack.h>
#include <timeros/virtio_net.h>

static int receive_calls;

int virtio_net_get_mac(u8 *mac)
{
    static const u8 expected[] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
    for (int i = 0; i < 6; i++)
        mac[i] = expected[i];
    return 0;
}

int virtio_net_receive(void *frame, u32 capacity, u32 *length, u64 deadline)
{
    (void)frame;
    (void)capacity;
    (void)length;
    (void)deadline;
    receive_calls++;
    return -1;
}

int virtio_net_send(const void *frame, u32 length)
{
    (void)frame;
    (void)length;
    return 0;
}

u64 net_stack_test_now(void)
{
    return 100;
}

int main(void)
{
    assert(net_stack_init() == NET_ERR_OK);
    netif_t *netif = net_stack_default();
    assert(netif != 0);
    assert(netif->state == NETIF_ACTIVE);
    assert(netif_get_default() == netif);
    assert(netif->hwaddr.len == 6);
    assert(netif->mtu == 1500);
    assert(netif->ipaddr.q_addr == 0xc0a86402U);
    assert(net_stack_poll_once(netif, 1234) == NET_ERR_TMO);
    assert(receive_calls == 1);
    return 0;
}
