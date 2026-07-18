#ifndef NLIST_H
#define NLIST_H

typedef struct _nlist_node_t {
    struct _nlist_node_t *next;
    struct _nlist_node_t *pre;
} nlist_node_t;

static inline void nlist_node_init(nlist_node_t *node)
{
    if (node != 0)
        node->pre = node->next = 0;
}

static inline nlist_node_t *nlist_node_next(nlist_node_t *node)
{
    return node != 0 ? node->next : 0;
}

static inline nlist_node_t *nlist_node_pre(nlist_node_t *node)
{
    return node != 0 ? node->pre : 0;
}

static inline void nlist_node_set_next(nlist_node_t *pre,
                                       nlist_node_t *next)
{
    if (pre != 0)
        pre->next = next;
}

typedef struct _nlist_t {
    nlist_node_t *first;
    nlist_node_t *last;
    int count;
} nlist_t;

void nlist_init(nlist_t *list);

static inline int nlist_is_empty(nlist_t *list)
{
    return list == 0 || list->count == 0;
}

static inline int nlist_count(nlist_t *list)
{
    return list != 0 ? list->count : 0;
}

static inline nlist_node_t *nlist_first(nlist_t *list)
{
    return list != 0 ? list->first : 0;
}

static inline nlist_node_t *nlist_last(nlist_t *list)
{
    return list != 0 ? list->last : 0;
}

#define noffset_in_parent(parent_type, node_name) \
        ((char *)&(((parent_type *)0)->node_name))

#define noffset_to_parent(node, parent_type, node_name) \
        ((char *)(node) - noffset_in_parent(parent_type, node_name))

#define nlist_entry(node, parent_type, node_name) \
        ((parent_type *)((node) != 0 ? \
                         noffset_to_parent((node), parent_type, node_name) : 0))

#define nlist_for_each(node, list) \
        for ((node) = nlist_first((list)); (node) != 0; \
             (node) = (node)->next)

void nlist_insert_first(nlist_t *list, nlist_node_t *node);
nlist_node_t *nlist_remove(nlist_t *list, nlist_node_t *node);
void nlist_insert_last(nlist_t *list, nlist_node_t *node);
void nlist_insert_after(nlist_t *list, nlist_node_t *pre, nlist_node_t *node);

static inline nlist_node_t *nlist_remove_first(nlist_t *list)
{
    nlist_node_t *first = nlist_first(list);

    if (first != 0)
        nlist_remove(list, first);
    return first;
}

static inline nlist_node_t *nlist_remove_last(nlist_t *list)
{
    nlist_node_t *last = nlist_last(list);

    if (last != 0)
        nlist_remove(list, last);
    return last;
}

#endif
