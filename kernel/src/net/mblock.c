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
    block->alloc_sem = SYS_SEM_INVALID;
    u8 *cursor = (u8 *)mem;
    for (int i = 0; i < count; i++, cursor += blk_size) {
        nlist_node_t *node = (nlist_node_t *)cursor;
        nlist_node_init(node);
        nlist_insert_last(&block->free_list, node);
    }
    if (type == NLOCKER_THREAD) {
        block->alloc_sem = sys_sem_create(count);
        if (block->alloc_sem == SYS_SEM_INVALID) {
            nlocker_destroy(&block->locker);
            return NET_ERR_MEM;
        }
    }
    return NET_ERR_OK;
}

void *mblock_alloc(mblock_t *block, int timeout)
{
    if (block == 0)
        return 0;
    if (block->locker.type == NLOCKER_THREAD &&
        sys_sem_wait(block->alloc_sem, timeout) < 0)
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
    if (block->locker.type == NLOCKER_THREAD)
        sys_sem_notify(block->alloc_sem);
}

void mblock_destroy(mblock_t *block)
{
    if (block != 0) {
        if (block->locker.type == NLOCKER_THREAD)
            sys_sem_free(block->alloc_sem);
        nlocker_destroy(&block->locker);
    }
}
