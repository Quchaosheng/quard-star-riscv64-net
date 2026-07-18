#ifndef TOS_VIRTQUEUE_H__
#define TOS_VIRTQUEUE_H__

#include <timeros/types.h>

#define VIRTQ_NUM 8
#define VIRTQ_PAGE_SIZE 4096

#define VRING_DESC_F_NEXT 1
#define VRING_DESC_F_WRITE 2

struct virtq_desc {
    u64 addr;
    u32 len;
    u16 flags;
    u16 next;
};

struct virtq_avail {
    u16 flags;
    u16 idx;
    u16 ring[VIRTQ_NUM];
    u16 unused;
};

struct virtq_used_elem {
    u32 id;
    u32 len;
};

struct virtq_used {
    u16 flags;
    u16 idx;
    struct virtq_used_elem ring[VIRTQ_NUM];
};

struct virtqueue {
    struct virtq_desc *desc;
    struct virtq_avail *avail;
    struct virtq_used *used;
    u8 free[VIRTQ_NUM];
    u8 active[VIRTQ_NUM];
    u16 used_idx;
    u16 free_count;
};

int virtq_init(struct virtqueue *q, void *pages, u16 num);
int virtq_alloc_chain(struct virtqueue *q, int count, int *indices);
int virtq_free_chain(struct virtqueue *q, u16 head);
void virtq_submit(struct virtqueue *q, u16 head);
int virtq_pop_used_len(struct virtqueue *q, u16 *head, u32 *length);
int virtq_pop_used(struct virtqueue *q, u16 *head);
int virtq_free_count(const struct virtqueue *q);

#endif
