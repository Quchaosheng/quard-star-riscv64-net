#ifndef TOS_NET_MBLOCK_H__
#define TOS_NET_MBLOCK_H__

#include <timeros/net/net_err.h>
#include <timeros/net/nlist.h>
#include <timeros/net/nlocker.h>

typedef struct {
    void *start;
    nlist_t free_list;
    nlocker_t locker;
} mblock_t;

net_err_t mblock_init(mblock_t *block, void *mem, int blk_size, int count,
                      nlocker_type_t type);
void *mblock_alloc(mblock_t *block, int timeout);
int mblock_free_cnt(mblock_t *block);
void mblock_free(mblock_t *block, void *item);
void mblock_destroy(mblock_t *block);

#endif
