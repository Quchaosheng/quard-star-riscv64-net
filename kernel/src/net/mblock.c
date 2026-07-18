#include <timeros/net/mblock.h>

net_err_t mblock_init(mblock_t *block, void *mem, int blk_size, int count,
                      nlocker_type_t type)
{
    if (block == 0 || mem == 0 || blk_size < (int)sizeof(nlist_node_t) ||
        count <= 0)
        return NET_ERR_PARAM;

    block->start = mem;
    nlist_init(&block->free_list);
    nlocker_init(&block->locker, type);
    u8 *cursor = (u8 *)mem;
    for (int i = 0; i < count; i++, cursor += blk_size) {
        nlist_node_t *node = (nlist_node_t *)cursor;
        nlist_node_init(node);
        nlist_insert_last(&block->free_list, node);
    }
    return NET_ERR_OK;
}

void *mblock_alloc(mblock_t *block, int timeout)
{
    (void)timeout;
    if (block == 0)
        return 0;
    nlocker_lock(&block->locker);
    nlist_node_t *node = nlist_remove_first(&block->free_list);
    nlocker_unlock(&block->locker);
    return node;
}

int mblock_free_cnt(mblock_t *block)
{
    if (block == 0)
        return -1;
    nlocker_lock(&block->locker);
    int count = nlist_count(&block->free_list);
    nlocker_unlock(&block->locker);
    return count;
}

void mblock_free(mblock_t *block, void *item)
{
    if (block == 0 || item == 0)
        return;
    nlocker_lock(&block->locker);
    nlist_insert_last(&block->free_list, (nlist_node_t *)item);
    nlocker_unlock(&block->locker);
}

void mblock_destroy(mblock_t *block)
{
    if (block != 0)
        nlocker_destroy(&block->locker);
}
