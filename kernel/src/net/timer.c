#include <timeros/net/timer.h>

typedef struct _timer_pending_t {
    nlist_t *list;
    struct _timer_pending_t *previous;
} timer_pending_t;

static nlist_t timer_list;
static timer_pending_t *pending_timers;

static int timer_is_scheduled(net_timer_t *timer)
{
    nlist_node_t *node;
    timer_pending_t *pending;

    nlist_for_each(node, &timer_list) {
        if (nlist_entry(node, net_timer_t, node) == timer)
            return 1;
    }

    for (pending = pending_timers; pending != 0;
         pending = pending->previous) {
        nlist_for_each(node, pending->list) {
            if (nlist_entry(node, net_timer_t, node) == timer)
                return 1;
        }
    }
    return 0;
}

static void timer_name_copy(char *dest, const char *src)
{
    int i = 0;

    while (i < TIMER_NAME_SIZE - 1 && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static void insert_timer(net_timer_t *insert)
{
    nlist_node_t *node;
    nlist_node_t *pre = 0;

    nlist_for_each(node, &timer_list) {
        net_timer_t *curr = nlist_entry(node, net_timer_t, node);

        if (insert->curr > curr->curr) {
            insert->curr -= curr->curr;
        } else if (insert->curr == curr->curr) {
            insert->curr = 0;
            nlist_insert_after(&timer_list, node, &insert->node);
            return;
        } else {
            curr->curr -= insert->curr;
            if (pre != 0)
                nlist_insert_after(&timer_list, pre, &insert->node);
            else
                nlist_insert_first(&timer_list, &insert->node);
            return;
        }
        pre = node;
    }

    nlist_insert_last(&timer_list, &insert->node);
}

net_err_t net_timer_init(void)
{
    nlist_init(&timer_list);
    pending_timers = 0;
    return NET_ERR_OK;
}

net_err_t net_timer_add(net_timer_t *timer, const char *name,
                        timer_proc_t proc, void *arg, int ms, int flags)
{
    if (timer == 0 || name == 0 || proc == 0 || ms <= 0)
        return NET_ERR_PARAM;
    if (timer_is_scheduled(timer))
        return NET_ERR_EXIST;

    timer_name_copy(timer->name, name);
    timer->flags = flags;
    timer->curr = ms;
    timer->reload = ms;
    timer->proc = proc;
    timer->arg = arg;
    nlist_node_init(&timer->node);
    insert_timer(timer);
    return NET_ERR_OK;
}

void net_timer_remove(net_timer_t *timer)
{
    nlist_node_t *node;

    if (timer == 0)
        return;

    nlist_for_each(node, &timer_list) {
        net_timer_t *curr = nlist_entry(node, net_timer_t, node);

        if (curr == timer) {
            nlist_node_t *next = nlist_node_next(node);

            if (next != 0) {
                net_timer_t *next_timer =
                    nlist_entry(next, net_timer_t, node);
                next_timer->curr += curr->curr;
            }
            nlist_remove(&timer_list, node);
            return;
        }
    }
}

net_err_t net_timer_check_tmo(int diff_ms)
{
    nlist_t wait_list;
    nlist_node_t *node;
    timer_pending_t pending;

    if (diff_ms < 0)
        return NET_ERR_PARAM;

    nlist_init(&wait_list);
    pending.list = &wait_list;
    pending.previous = pending_timers;
    pending_timers = &pending;
    for (;;) {
        node = nlist_first(&timer_list);
        if (node == 0)
            break;
        net_timer_t *first = nlist_entry(node, net_timer_t, node);
        if (diff_ms == 0 && first->curr != 0)
            break;
        if (first->curr > diff_ms) {
            first->curr -= diff_ms;
            diff_ms = 0;
            break;
        }

        diff_ms -= first->curr;
        while ((node = nlist_first(&timer_list)) != 0) {
            net_timer_t *timer = nlist_entry(node, net_timer_t, node);
            if (timer->curr != 0)
                break;
            nlist_remove(&timer_list, node);
            nlist_insert_last(&wait_list, node);
        }
        while ((node = nlist_remove_first(&wait_list)) != 0) {
            net_timer_t *timer = nlist_entry(node, net_timer_t, node);

            timer->proc(timer, timer->arg);
            if ((timer->flags & NET_TIMER_RELOAD) != 0 &&
                !timer_is_scheduled(timer)) {
                timer->curr = timer->reload;
                insert_timer(timer);
            }
        }
    }
    pending_timers = pending.previous;
    return NET_ERR_OK;
}

int net_timer_first_tmo(void)
{
    nlist_node_t *node = nlist_first(&timer_list);

    if (node == 0)
        return 0;
    return nlist_entry(node, net_timer_t, node)->curr;
}
