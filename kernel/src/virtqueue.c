#include <timeros/virtqueue.h>

int virtq_init(struct virtqueue *q, void *pages, u16 num)
{
    if (q == 0 || pages == 0 || num != VIRTQ_NUM ||
        ((uintptr_t)pages & (VIRTQ_PAGE_SIZE - 1)) != 0)
        return -1;

    u8 *bytes = pages;
    for (u32 i = 0; i < 2 * VIRTQ_PAGE_SIZE; i++)
        bytes[i] = 0;

    q->desc = (struct virtq_desc *)pages;
    q->avail = (struct virtq_avail *)(bytes +
                                      VIRTQ_NUM * sizeof(struct virtq_desc));
    q->used = (struct virtq_used *)(bytes + VIRTQ_PAGE_SIZE);
    q->used_idx = 0;
    q->free_count = VIRTQ_NUM;
    for (int i = 0; i < VIRTQ_NUM; i++) {
        q->free[i] = 1;
        q->active[i] = 0;
    }
    return 0;
}

static int alloc_desc(struct virtqueue *q)
{
    for (int i = 0; i < VIRTQ_NUM; i++) {
        if (q->free[i]) {
            q->free[i] = 0;
            q->free_count--;
            q->desc[i].addr = 0;
            q->desc[i].len = 0;
            q->desc[i].flags = 0;
            q->desc[i].next = 0;
            return i;
        }
    }
    return -1;
}

static void release_desc(struct virtqueue *q, int index)
{
    q->desc[index].addr = 0;
    q->desc[index].len = 0;
    q->desc[index].flags = 0;
    q->desc[index].next = 0;
    q->free[index] = 1;
    q->free_count++;
}

int virtq_alloc_chain(struct virtqueue *q, int count, int *indices)
{
    if (q == 0 || indices == 0 || count < 1 || count > VIRTQ_NUM)
        return -1;

    int allocated = 0;
    while (allocated < count) {
        int index = alloc_desc(q);
        if (index < 0) {
            for (int i = 0; i < allocated; i++)
                release_desc(q, indices[i]);
            return -1;
        }
        indices[allocated] = index;
        if (allocated > 0) {
            int previous = indices[allocated - 1];
            q->desc[previous].flags |= VRING_DESC_F_NEXT;
            q->desc[previous].next = (u16)index;
        }
        allocated++;
    }
    return 0;
}

int virtq_free_chain(struct virtqueue *q, u16 head)
{
    if (q == 0 || head >= VIRTQ_NUM || q->free[head] || q->active[head])
        return -1;

    u16 chain[VIRTQ_NUM];
    u8 seen[VIRTQ_NUM] = {0};
    int count = 0;
    u16 index = head;

    for (;;) {
        if (index >= VIRTQ_NUM || q->free[index] || seen[index] ||
            count >= VIRTQ_NUM)
            return -1;
        seen[index] = 1;
        chain[count++] = index;
        if ((q->desc[index].flags & VRING_DESC_F_NEXT) == 0)
            break;
        index = q->desc[index].next;
    }

    for (int i = 0; i < count; i++)
        release_desc(q, chain[i]);
    return count;
}

void virtq_submit(struct virtqueue *q, u16 head)
{
    if (q == 0 || head >= VIRTQ_NUM || q->free[head] || q->active[head])
        return;
    q->active[head] = 1;
    u16 avail_idx = __atomic_load_n(&q->avail->idx, __ATOMIC_RELAXED);
    q->avail->ring[avail_idx % VIRTQ_NUM] = head;
    __atomic_store_n(&q->avail->idx, (u16)(avail_idx + 1),
                     __ATOMIC_RELEASE);
}

int virtq_pop_used_len(struct virtqueue *q, u16 *head, u32 *length)
{
    if (q == 0 || head == 0 || length == 0)
        return -1;
    u16 used_idx = __atomic_load_n(&q->used->idx, __ATOMIC_ACQUIRE);
    if (q->used_idx == used_idx)
        return 0;

    struct virtq_used_elem *used =
        &q->used->ring[q->used_idx % VIRTQ_NUM];
    u32 id = used->id;
    u32 used_length = used->len;
    q->used_idx++;
    if (id >= VIRTQ_NUM)
        return -1;
    if (!q->active[id])
        return -1;

    q->active[id] = 0;
    *head = (u16)id;
    *length = used_length;
    return 1;
}

int virtq_pop_used(struct virtqueue *q, u16 *head)
{
    u32 length;
    return virtq_pop_used_len(q, head, &length);
}

int virtq_free_count(const struct virtqueue *q)
{
    return q == 0 ? -1 : q->free_count;
}
