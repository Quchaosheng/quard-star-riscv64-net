#include <timeros/virtio_net_completion.h>

void net_completion_init(struct net_completion_ring *ring)
{
    if (ring == 0)
        return;
    ring->read_index = 0;
    ring->write_index = 0;
    ring->count = 0;
}

void net_completion_reset(struct net_completion_ring *ring)
{
    net_completion_init(ring);
}

int net_completion_push(struct net_completion_ring *ring, u16 slot)
{
    if (ring == 0 || ring->count == NET_COMPLETION_CAPACITY)
        return -1;
    ring->slots[ring->write_index] = slot;
    ring->write_index = (u16)((ring->write_index + 1) %
                              NET_COMPLETION_CAPACITY);
    ring->count++;
    return 0;
}

int net_completion_pop(struct net_completion_ring *ring, u16 *slot)
{
    if (ring == 0 || slot == 0)
        return -1;
    if (ring->count == 0)
        return 0;
    *slot = ring->slots[ring->read_index];
    ring->read_index = (u16)((ring->read_index + 1) %
                             NET_COMPLETION_CAPACITY);
    ring->count--;
    return 1;
}

int net_completion_count(const struct net_completion_ring *ring)
{
    return ring == 0 ? -1 : ring->count;
}
