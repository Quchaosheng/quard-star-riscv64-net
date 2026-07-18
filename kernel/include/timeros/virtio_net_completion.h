#ifndef TOS_VIRTIO_NET_COMPLETION_H__
#define TOS_VIRTIO_NET_COMPLETION_H__

#include <timeros/virtqueue.h>

#define NET_COMPLETION_CAPACITY VIRTQ_NUM

struct net_completion_ring {
    u16 slots[NET_COMPLETION_CAPACITY];
    u16 read_index;
    u16 write_index;
    u16 count;
};

void net_completion_init(struct net_completion_ring *ring);
void net_completion_reset(struct net_completion_ring *ring);
int net_completion_push(struct net_completion_ring *ring, u16 slot);
int net_completion_pop(struct net_completion_ring *ring, u16 *slot);
int net_completion_count(const struct net_completion_ring *ring);

#endif
