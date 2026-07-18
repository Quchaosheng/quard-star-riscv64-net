#ifndef PKTBUF_H
#define PKTBUF_H

#include <stdint.h>

#include "net_cfg.h"
#include "net_err.h"
#include "nlist.h"

typedef struct _pktblk_t {
    nlist_node_t node;
    int size;
    uint8_t *data;
    uint8_t payload[PKTBUF_BLK_SIZE];
} pktblk_t;

typedef struct _pktbuf_t {
    int total_size;
    nlist_t blk_list;
    nlist_node_t node;
    int ref;
    int pos;
    pktblk_t *curr_blk;
    uint8_t *blk_offset;
} pktbuf_t;

static inline pktblk_t *pktbuf_blk_next(pktblk_t *blk)
{
    return blk != 0 ? nlist_entry(nlist_node_next(&blk->node),
                                  pktblk_t, node) : 0;
}

static inline pktblk_t *pktbuf_first_blk(pktbuf_t *buf)
{
    return buf != 0 ? nlist_entry(nlist_first(&buf->blk_list),
                                  pktblk_t, node) : 0;
}

static inline pktblk_t *pktbuf_last_blk(pktbuf_t *buf)
{
    return buf != 0 ? nlist_entry(nlist_last(&buf->blk_list),
                                  pktblk_t, node) : 0;
}

static inline int pktbuf_total(pktbuf_t *buf)
{
    return buf != 0 ? buf->total_size : 0;
}

static inline uint8_t *pktbuf_data(pktbuf_t *buf)
{
    pktblk_t *first = pktbuf_first_blk(buf);

    return first != 0 ? first->data : 0;
}

net_err_t pktbuf_init(void);
pktbuf_t *pktbuf_alloc(int size);
void pktbuf_free(pktbuf_t *buf);
net_err_t pktbuf_add_header(pktbuf_t *buf, int size, int cont);
net_err_t pktbuf_remove_header(pktbuf_t *buf, int size);
net_err_t pktbuf_resize(pktbuf_t *buf, int to_size);
net_err_t pktbuf_join(pktbuf_t *dest, pktbuf_t *src);
net_err_t pktbuf_set_cont(pktbuf_t *buf, int size);

void pktbuf_reset_acc(pktbuf_t *buf);
int pktbuf_write(pktbuf_t *buf, const uint8_t *src, int size);
int pktbuf_read(pktbuf_t *buf, uint8_t *dest, int size);
net_err_t pktbuf_seek(pktbuf_t *buf, int offset);
net_err_t pktbuf_copy(pktbuf_t *dest, pktbuf_t *src, int size);
net_err_t pktbuf_fill(pktbuf_t *buf, uint8_t value, int size);
void pktbuf_inc_ref(pktbuf_t *buf);
uint16_t pktbuf_checksum16(pktbuf_t *buf, int size, uint32_t pre_sum,
                           int complement);

#endif
