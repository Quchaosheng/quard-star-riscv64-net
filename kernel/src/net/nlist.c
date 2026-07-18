#include "nlist.h"

void nlist_init(nlist_t *list)
{
    if (list != 0) {
        list->first = 0;
        list->last = 0;
        list->count = 0;
    }
}

void nlist_insert_first(nlist_t *list, nlist_node_t *node)
{
    if (list == 0 || node == 0)
        return;

    node->pre = 0;
    node->next = list->first;
    if (list->first != 0)
        list->first->pre = node;
    else
        list->last = node;
    list->first = node;
    list->count++;
}

nlist_node_t *nlist_remove(nlist_t *list, nlist_node_t *node)
{
    if (list == 0 || node == 0 || list->count == 0)
        return 0;

    if (node == list->first)
        list->first = node->next;
    if (node == list->last)
        list->last = node->pre;
    if (node->pre != 0)
        node->pre->next = node->next;
    if (node->next != 0)
        node->next->pre = node->pre;

    node->pre = 0;
    node->next = 0;
    list->count--;
    return node;
}

void nlist_insert_last(nlist_t *list, nlist_node_t *node)
{
    if (list == 0 || node == 0)
        return;

    node->next = 0;
    node->pre = list->last;
    if (list->last != 0)
        list->last->next = node;
    else
        list->first = node;
    list->last = node;
    list->count++;
}

void nlist_insert_after(nlist_t *list, nlist_node_t *pre,
                        nlist_node_t *node)
{
    if (list == 0 || pre == 0 || node == 0)
        return;
    if (list->count == 0) {
        nlist_insert_first(list, node);
        return;
    }

    node->pre = pre;
    node->next = pre->next;
    if (pre->next != 0)
        pre->next->pre = node;
    else
        list->last = node;
    pre->next = node;
    list->count++;
}
