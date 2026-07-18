#include <assert.h>
#include <timeros/virtqueue.h>

static void test_allocate_rollback_and_reclaim(void)
{
    static unsigned char pages[2 * 4096] __attribute__((aligned(4096)));
    struct virtqueue q;
    int chain[VIRTQ_NUM];
    int exhausted[VIRTQ_NUM];

    assert(virtq_init(&q, pages, VIRTQ_NUM) == 0);
    assert(virtq_free_count(&q) == VIRTQ_NUM);
    assert(virtq_alloc_chain(&q, 3, chain) == 0);
    assert(virtq_free_count(&q) == VIRTQ_NUM - 3);
    assert(virtq_alloc_chain(&q, VIRTQ_NUM, exhausted) == -1);
    assert(virtq_free_count(&q) == VIRTQ_NUM - 3);
    assert(virtq_free_chain(&q, (u16)chain[0]) == 3);
    assert(virtq_free_count(&q) == VIRTQ_NUM);
}

static void test_ring_wrap_and_invalid_used_id(void)
{
    static unsigned char pages[2 * 4096] __attribute__((aligned(4096)));
    struct virtqueue q;
    int chain[1];
    u16 head;

    assert(virtq_init(&q, pages, VIRTQ_NUM) == 0);
    for (int i = 0; i < VIRTQ_NUM * 3; i++) {
        assert(virtq_alloc_chain(&q, 1, chain) == 0);
        virtq_submit(&q, (u16)chain[0]);
        q.used->ring[q.used->idx % VIRTQ_NUM].id = (u32)chain[0];
        q.used->idx++;
        assert(virtq_pop_used(&q, &head) == 1);
        assert(head == (u16)chain[0]);
        assert(virtq_free_chain(&q, head) == 1);
    }
    q.used->ring[q.used->idx % VIRTQ_NUM].id = VIRTQ_NUM;
    q.used->idx++;
    assert(virtq_pop_used(&q, &head) == -1);
}

int main(void)
{
    test_allocate_rollback_and_reclaim();
    test_ring_wrap_and_invalid_used_id();
    return 0;
}
