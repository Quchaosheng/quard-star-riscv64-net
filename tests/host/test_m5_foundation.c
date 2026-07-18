#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <timeros/net/fixq.h>
#include <timeros/net/mblock.h>
#include <timeros/net/nlist.h>
#include <timeros/net/pktbuf.h>

static void test_nlist_and_mblock(void)
{
    nlist_t list;
    nlist_node_t nodes[3];
    nlist_init(&list);
    for (int i = 0; i < 3; i++) {
        nlist_node_init(&nodes[i]);
        nlist_insert_last(&list, &nodes[i]);
    }
    assert(nlist_count(&list) == 3);
    assert(nlist_remove_first(&list) == &nodes[0]);
    assert(nlist_remove_last(&list) == &nodes[2]);
    assert(nlist_remove_first(&list) == &nodes[1]);
    assert(nlist_is_empty(&list));

    unsigned char storage[3][64];
    mblock_t blocks;
    assert(mblock_init(&blocks, storage, sizeof(storage[0]), 3,
                       NLOCKER_NONE) == NET_ERR_OK);
    void *a = mblock_alloc(&blocks, -1);
    void *b = mblock_alloc(&blocks, -1);
    void *c = mblock_alloc(&blocks, -1);
    assert(a && b && c);
    assert(mblock_alloc(&blocks, -1) == 0);
    assert(mblock_free_cnt(&blocks) == 0);
    mblock_free(&blocks, b);
    assert(mblock_free_cnt(&blocks) == 1);
    mblock_destroy(&blocks);
}

static void test_fixq(void)
{
    void *items[2];
    int first = 1;
    int second = 2;
    fixq_t queue;
    assert(fixq_init(&queue, items, 2, NLOCKER_NONE) == NET_ERR_OK);
    assert(fixq_send(&queue, &first, -1) == NET_ERR_OK);
    assert(fixq_send(&queue, &second, -1) == NET_ERR_OK);
    assert(fixq_send(&queue, &first, -1) == NET_ERR_FULL);
    assert(fixq_count(&queue) == 2);
    assert(fixq_recv(&queue, -1) == &first);
    assert(fixq_recv(&queue, -1) == &second);
    assert(fixq_recv(&queue, -1) == 0);
    fixq_destroy(&queue);
}

static void test_pktbuf_across_blocks(void)
{
    uint8_t source[2048];
    uint8_t result[2048];
    for (int i = 0; i < (int)sizeof(source); i++)
        source[i] = (uint8_t)(i ^ 0x5a);

    assert(pktbuf_init() == NET_ERR_OK);
    pktbuf_t *buf = pktbuf_alloc(sizeof(source));
    assert(buf != 0);
    assert(pktbuf_write(buf, source, sizeof(source)) == NET_ERR_OK);
    pktbuf_reset_acc(buf);
    assert(pktbuf_read(buf, result, sizeof(result)) == NET_ERR_OK);
    assert(memcmp(source, result, sizeof(source)) == 0);

    assert(pktbuf_add_header(buf, 32, 1) == NET_ERR_OK);
    pktbuf_reset_acc(buf);
    assert(pktbuf_fill(buf, 0xa5, 32) == NET_ERR_OK);
    assert(pktbuf_remove_header(buf, 32) == NET_ERR_OK);
    assert(pktbuf_resize(buf, 3000) == NET_ERR_OK);
    assert(pktbuf_total(buf) == 3000);
    assert(pktbuf_resize(buf, 1500) == NET_ERR_OK);
    assert(pktbuf_total(buf) == 1500);

    pktbuf_inc_ref(buf);
    pktbuf_free(buf);
    pktbuf_free(buf);
}

static void test_pktbuf_boundaries(void)
{
    assert(pktbuf_alloc(-1) == 0);

    pktbuf_t *buf = pktbuf_alloc(4);
    assert(buf != 0);
    assert(pktbuf_resize(buf, -1) == NET_ERR_PARAM);
    assert(pktbuf_add_header(buf, -1, 1) == NET_ERR_PARAM);
    assert(pktbuf_remove_header(buf, 5) == NET_ERR_SIZE);
    assert(pktbuf_write(buf, 0, -1) == NET_ERR_PARAM);
    assert(pktbuf_read(buf, 0, -1) == NET_ERR_PARAM);
    assert(pktbuf_fill(buf, 0, -1) == NET_ERR_PARAM);
    assert(pktbuf_copy(buf, buf, -1) == NET_ERR_PARAM);
    assert(pktbuf_set_cont(buf, -1) == NET_ERR_PARAM);
    assert(pktbuf_seek(buf, 4) == NET_ERR_OK);
    pktbuf_free(buf);

    buf = pktbuf_alloc(0);
    assert(buf != 0);
    assert(pktbuf_add_header(buf, 8, 1) == NET_ERR_OK);
    assert(pktbuf_fill(buf, 0x5a, 8) == NET_ERR_OK);
    assert(pktbuf_seek(buf, 8) == NET_ERR_OK);
    assert(pktbuf_resize(buf, 0) == NET_ERR_OK);
    assert(pktbuf_total(buf) == 0);
    assert(pktbuf_write(buf, 0, 0) == NET_ERR_OK);
    assert(pktbuf_read(buf, 0, 0) == NET_ERR_OK);
    assert(pktbuf_fill(buf, 0, 0) == NET_ERR_OK);
    assert(pktbuf_join(buf, buf) == NET_ERR_PARAM);
    pktbuf_free(buf);
    pktbuf_free(0);
}

int main(void)
{
    test_nlist_and_mblock();
    test_fixq();
    test_pktbuf_across_blocks();
    test_pktbuf_boundaries();
    return 0;
}
