#include <assert.h>
#include <string.h>

#include <timeros/net/timer.h>

typedef struct {
    int id;
    int calls;
    int clear_reload;
} callback_state_t;

typedef struct {
    int calls;
    int reload_ms;
} self_readd_state_t;

static int callback_order[16];
static int callback_order_count;

static void record_callback(net_timer_t *timer, void *arg)
{
    callback_state_t *state = arg;

    state->calls++;
    callback_order[callback_order_count++] = state->id;
    if (state->clear_reload)
        timer->flags &= ~NET_TIMER_RELOAD;
}

static void self_readd_callback(net_timer_t *timer, void *arg)
{
    self_readd_state_t *state = arg;

    state->calls++;
    assert(net_timer_add(timer, "self-readd", self_readd_callback, state,
                         state->reload_ms, NET_TIMER_RELOAD) == NET_ERR_OK);
}

static void reset_order(void)
{
    callback_order_count = 0;
    memset(callback_order, 0, sizeof(callback_order));
}

static void test_reload_uses_next_check_baseline(void)
{
    net_timer_t slow;
    net_timer_t fast;
    callback_state_t slow_state = { .id = 1 };
    callback_state_t fast_state = { .id = 2 };

    assert(net_timer_init() == NET_ERR_OK);
    assert(net_timer_first_tmo() == 0);
    assert(net_timer_add(&slow, "slow", record_callback, &slow_state,
                         30, 0) == NET_ERR_OK);
    assert(net_timer_add(&fast, "fast", record_callback, &fast_state,
                         10, NET_TIMER_RELOAD) == NET_ERR_OK);
    assert(net_timer_first_tmo() == 10);

    assert(net_timer_check_tmo(10) == NET_ERR_OK);
    assert(fast_state.calls == 1);
    assert(slow_state.calls == 0);

    assert(net_timer_check_tmo(20) == NET_ERR_OK);
    assert(fast_state.calls == 2);
    assert(slow_state.calls == 1);

    assert(net_timer_check_tmo(10) == NET_ERR_OK);
    assert(fast_state.calls == 3);
    assert(net_timer_first_tmo() == 10);
    net_timer_remove(&fast);
}

static void test_equal_deadlines_follow_baseline_order(void)
{
    net_timer_t timers[3];
    callback_state_t states[3] = {
        { .id = 10 }, { .id = 20 }, { .id = 30 },
    };

    assert(net_timer_init() == NET_ERR_OK);
    reset_order();
    for (int i = 0; i < 3; i++)
        assert(net_timer_add(&timers[i], "same", record_callback, &states[i],
                             10, 0) == NET_ERR_OK);

    assert(net_timer_check_tmo(10) == NET_ERR_OK);
    assert(callback_order_count == 3);
    assert(callback_order[0] == 10);
    assert(callback_order[1] == 30);
    assert(callback_order[2] == 20);
    assert(net_timer_first_tmo() == 0);
}

static void test_remove_preserves_remaining_deadline(void)
{
    net_timer_t head;
    net_timer_t tail;
    net_timer_t inactive = { 0 };
    callback_state_t head_state = { .id = 1 };
    callback_state_t tail_state = { .id = 2 };

    assert(net_timer_init() == NET_ERR_OK);
    assert(net_timer_add(&head, "head", record_callback, &head_state,
                         10, 0) == NET_ERR_OK);
    assert(net_timer_add(&tail, "tail", record_callback, &tail_state,
                         30, 0) == NET_ERR_OK);

    net_timer_remove(&inactive);
    net_timer_remove(0);
    net_timer_remove(&head);
    net_timer_remove(&head);
    assert(net_timer_first_tmo() == 30);
    assert(net_timer_check_tmo(29) == NET_ERR_OK);
    assert(tail_state.calls == 0);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    assert(tail_state.calls == 1);
    assert(head_state.calls == 0);
    assert(net_timer_first_tmo() == 0);
}

static void test_reload_can_be_disabled_by_callback(void)
{
    net_timer_t timer;
    callback_state_t state = { .id = 1, .clear_reload = 1 };

    assert(net_timer_init() == NET_ERR_OK);
    assert(net_timer_add(&timer, "once-after-callback", record_callback,
                         &state, 5, NET_TIMER_RELOAD) == NET_ERR_OK);
    assert(net_timer_check_tmo(5) == NET_ERR_OK);
    assert(state.calls == 1);
    assert(net_timer_first_tmo() == 0);
    assert(net_timer_check_tmo(50) == NET_ERR_OK);
    assert(state.calls == 1);
}

static void test_arguments_and_bounded_name(void)
{
    net_timer_t timer;
    callback_state_t state = { .id = 1 };
    const char long_name[] =
        "this timer name is deliberately much longer than its destination";

    assert(net_timer_init() == NET_ERR_OK);
    assert(net_timer_add(0, "timer", record_callback, &state, 1, 0) ==
           NET_ERR_PARAM);
    assert(net_timer_add(&timer, 0, record_callback, &state, 1, 0) ==
           NET_ERR_PARAM);
    assert(net_timer_add(&timer, "timer", 0, &state, 1, 0) ==
           NET_ERR_PARAM);
    assert(net_timer_add(&timer, "timer", record_callback, &state, 0, 0) ==
           NET_ERR_PARAM);
    assert(net_timer_add(&timer, "timer", record_callback, &state, -1, 0) ==
           NET_ERR_PARAM);
    assert(net_timer_check_tmo(-1) == NET_ERR_PARAM);
    assert(net_timer_check_tmo(0) == NET_ERR_OK);

    assert(net_timer_add(&timer, long_name, record_callback, &state, 5, 0) ==
           NET_ERR_OK);
    assert(timer.name[TIMER_NAME_SIZE - 1] == '\0');
    for (int i = 0; i < TIMER_NAME_SIZE - 1; i++)
        assert(timer.name[i] == long_name[i]);
    assert(net_timer_check_tmo(0) == NET_ERR_OK);
    assert(state.calls == 0);
    net_timer_remove(&timer);
    assert(net_timer_first_tmo() == 0);
}

static void test_duplicate_active_add_preserves_schedule(void)
{
    net_timer_t timer;
    callback_state_t state = { .id = 1 };

    assert(net_timer_init() == NET_ERR_OK);
    assert(net_timer_add(&timer, "original", record_callback, &state,
                         10, 0) == NET_ERR_OK);
    assert(net_timer_add(&timer, "replacement", record_callback, &state,
                         1, NET_TIMER_RELOAD) == NET_ERR_EXIST);
    assert(strcmp(timer.name, "original") == 0);
    assert(timer.flags == 0);
    assert(timer.curr == 10);
    assert(timer.reload == 10);
    assert(timer.proc == record_callback);
    assert(timer.arg == &state);
    assert(net_timer_first_tmo() == 10);

    assert(net_timer_check_tmo(9) == NET_ERR_OK);
    assert(state.calls == 0);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    assert(state.calls == 1);
    assert(net_timer_first_tmo() == 0);
}

static void test_callback_can_readd_itself_once(void)
{
    net_timer_t timer;
    self_readd_state_t state = { .reload_ms = 7 };

    assert(net_timer_init() == NET_ERR_OK);
    assert(net_timer_add(&timer, "one-shot", self_readd_callback, &state,
                         3, 0) == NET_ERR_OK);
    assert(net_timer_check_tmo(3) == NET_ERR_OK);
    assert(state.calls == 1);
    assert(net_timer_first_tmo() == 7);

    assert(net_timer_check_tmo(6) == NET_ERR_OK);
    assert(state.calls == 1);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    assert(state.calls == 2);
    assert(net_timer_first_tmo() == 7);
    net_timer_remove(&timer);
    assert(net_timer_first_tmo() == 0);
}

int main(void)
{
    test_reload_uses_next_check_baseline();
    test_equal_deadlines_follow_baseline_order();
    test_remove_preserves_remaining_deadline();
    test_reload_can_be_disabled_by_callback();
    test_arguments_and_bounded_name();
    test_duplicate_active_add_preserves_schedule();
    test_callback_can_readd_itself_once();
    return 0;
}
