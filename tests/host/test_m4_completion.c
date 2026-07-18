#include <assert.h>

#include <timeros/virtio_net_completion.h>

static void test_full_empty_and_order(void)
{
    struct net_completion_ring ring;
    u16 slot = 0xffff;

    net_completion_init(&ring);
    assert(net_completion_count(&ring) == 0);
    for (u16 i = 0; i < NET_COMPLETION_CAPACITY; i++)
        assert(net_completion_push(&ring, i) == 0);
    assert(net_completion_count(&ring) == NET_COMPLETION_CAPACITY);
    assert(net_completion_push(&ring, 99) == -1);

    for (u16 i = 0; i < NET_COMPLETION_CAPACITY; i++) {
        assert(net_completion_pop(&ring, &slot) == 1);
        assert(slot == i);
    }
    assert(net_completion_pop(&ring, &slot) == 0);
    assert(net_completion_count(&ring) == 0);
}

static void test_wrap_and_reset(void)
{
    struct net_completion_ring ring;
    u16 slot;

    net_completion_init(&ring);
    for (int cycle = 0; cycle < NET_COMPLETION_CAPACITY * 3; cycle++) {
        assert(net_completion_push(&ring, (u16)(cycle + 3)) == 0);
        assert(net_completion_pop(&ring, &slot) == 1);
        assert(slot == (u16)(cycle + 3));
    }

    assert(net_completion_push(&ring, 7) == 0);
    net_completion_reset(&ring);
    assert(net_completion_count(&ring) == 0);
    assert(net_completion_pop(&ring, &slot) == 0);
}

static void test_invalid_arguments(void)
{
    struct net_completion_ring ring;
    u16 slot;

    net_completion_init(&ring);
    assert(net_completion_push(0, 1) == -1);
    assert(net_completion_pop(0, &slot) == -1);
    assert(net_completion_pop(&ring, 0) == -1);
    assert(net_completion_count(0) == -1);
}

int main(void)
{
    test_full_empty_and_order();
    test_wrap_and_reset();
    test_invalid_arguments();
    return 0;
}
