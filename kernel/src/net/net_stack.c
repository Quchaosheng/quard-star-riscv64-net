#include <timeros/net/net_stack.h>

#include <timeros/net/arp.h>
#include <timeros/net/ether.h>
#include <timeros/net/fixq.h>
#include <timeros/net/icmpv4.h>
#include <timeros/net/ipv4.h>
#include <timeros/net/loop.h>
#include <timeros/net/net_cfg.h>
#include <timeros/net/net_exec.h>
#include <timeros/net/net_sys.h>
#include <timeros/net/netif_virtio.h>
#include <timeros/net/pktbuf.h>
#include <timeros/net/socket.h>
#include <timeros/net/tcp.h>
#include <timeros/net/timer.h>
#include <timeros/net/udp.h>

#ifdef QS_M5_TEST
#include <timeros/memory.h>
#include <timeros/selftest.h>
extern int printk(const char *format, ...);
#endif

#ifdef __riscv
#include <timeros/riscv.h>
static u64 net_stack_now(void)
{
    return r_mtime();
}
#else
extern u64 net_stack_test_now(void);
static u64 net_stack_now(void)
{
    return net_stack_test_now();
}
#endif

#ifdef QS_M5_TEST
#define NET_STACK_PROBE_TIMEOUT_MS 20000
#define NET_STACK_PROBE_IDENTIFIER 0x4d35
#define NET_STACK_PROBE_SEQUENCE 1
#endif

#ifdef QS_M6A_TEST
#define NET_STACK_LOOP_IDENTIFIER 0x6d36
#define NET_STACK_LOOP_SEQUENCE 1

static void net_stack_probe_fail(int code);

static int loop_probe_started;
static int loop_probe_reported;
static int queue_probe_reported;

static void net_stack_queue_probe(void)
{
    if (queue_probe_reported)
        return;

    fixq_t queue;
    void *storage[1];
    int first;
    int second;
    net_time_t wait_started;
    if (fixq_init(&queue, storage, 1, NLOCKER_THREAD) < 0 ||
        fixq_send(&queue, &first, -1) != NET_ERR_OK ||
        fixq_send(&queue, &second, -1) != NET_ERR_FULL) {
        net_stack_probe_fail(6);
    }
    sys_time_curr(&wait_started);
    if (fixq_send(&queue, &second, 20) != NET_ERR_TMO ||
        sys_time_goes(&wait_started) < 10 ||
        fixq_recv(&queue, -1) != &first) {
        net_stack_probe_fail(6);
    }
    fixq_destroy(&queue);
    queue_probe_reported = 1;
    printk("QS:M6_QUEUE_OK\n");
    m6_mark_queue();
}

static void net_stack_loop_probe(void)
{
    netif_t *loop = loop_get_netif();

    if (loop == 0)
        net_stack_probe_fail(7);
    if (!loop_probe_started) {
        static const u8 payload[] = "quard-star-m6a";
        if (icmpv4_out_echo(loop, &loop->ipaddr,
                            NET_STACK_LOOP_IDENTIFIER,
                            NET_STACK_LOOP_SEQUENCE, payload,
                            (int)sizeof(payload) - 1) < 0) {
            net_stack_probe_fail(8);
        }
        loop_probe_started = 1;
    }
}

static void net_stack_loop_report(void)
{
    icmpv4_stats_t stats;

    icmpv4_get_stats(&stats);
    if (!loop_probe_reported && stats.last_reply_identifier ==
        NET_STACK_LOOP_IDENTIFIER && stats.last_reply_sequence ==
        NET_STACK_LOOP_SEQUENCE) {
        loop_probe_reported = 1;
        printk("QS:M6_LOOP_OK\n");
        m6_mark_loop();
    }
}
#endif

static netif_t *stack_netif;
static int stack_initialized;

#ifdef QS_M5_TEST
static ipaddr_t probe_peer;
static int probe_started;
static int probe_arp_reported;
static int probe_ping_reported;
static u64 probe_deadline;

static void net_stack_probe_fail(int code)
{
    printk("QS:TEST_FAIL:m5-net:%d\n", code);
    *(volatile u32 *)(uintptr_t)QEMU_TEST_BASE = QEMU_TEST_FAIL;
    for (;;)
        asm volatile ("wfi");
}

static void net_stack_probe(netif_t *netif)
{
    if (netif == 0)
        return;
    if (!probe_started) {
        if (ipaddr_from_str(&probe_peer, "192.168.100.1") != NET_ERR_OK)
            net_stack_probe_fail(1);
        static const u8 payload[] = "quard-star-m5";
        net_err_t err = icmpv4_out_echo(netif, &probe_peer,
                                        NET_STACK_PROBE_IDENTIFIER,
                                        NET_STACK_PROBE_SEQUENCE, payload,
                                        (int)sizeof(payload) - 1);
        if (err < 0)
            net_stack_probe_fail(2);
        probe_started = 1;
        probe_deadline = net_stack_now() +
                         (u64)NET_STACK_PROBE_TIMEOUT_MS *
                         NET_TIME_TICKS_PER_MS;
    }

    if (!probe_arp_reported && arp_find(netif, &probe_peer) != 0) {
        probe_arp_reported = 1;
        printk("QS:M5_ARP_OK\n");
        m5_mark_net_arp();
    }

    icmpv4_stats_t stats;
    icmpv4_get_stats(&stats);
    if (!probe_ping_reported && stats.last_reply_identifier ==
        NET_STACK_PROBE_IDENTIFIER &&
        stats.last_reply_sequence == NET_STACK_PROBE_SEQUENCE) {
        probe_ping_reported = 1;
        printk("QS:M5_PING_OK\n");
        m5_mark_net_ping();
    }

    if (!probe_ping_reported && net_stack_now() >= probe_deadline)
        net_stack_probe_fail(3);
}
#endif

net_err_t net_stack_init(void)
{
    if (stack_initialized)
        return NET_ERR_STATE;
    net_err_t err = net_sys_init();
    if (err < 0)
        return err;
    err = net_exec_init();
    if (err < 0)
        return err;
    err = pktbuf_init();
    if (err < 0)
        return err;
    err = net_timer_init();
    if (err < 0)
        return err;
    err = netif_init();
    if (err < 0)
        return err;
    err = ether_init();
    if (err < 0)
        return err;
    err = arp_init();
    if (err < 0)
        return err;
    err = ipv4_init();
    if (err < 0)
        return err;
    err = tcp_init();
    if (err < 0)
        return err;
    err = icmpv4_init();
    if (err < 0)
        return err;
    err = udp_init();
    if (err < 0)
        return err;
    err = net_socket_init();
    if (err < 0)
        return err;
    err = loop_init();
    if (err < 0)
        return err;

    netif_t *netif = netif_open("eth0", &netif_virtio_ops, 0);
    if (netif == 0)
        return NET_ERR_IO;
    ipaddr_t ip;
    ipaddr_t mask;
    if (ipaddr_from_str(&ip, "192.168.100.2") != NET_ERR_OK ||
        ipaddr_from_str(&mask, "255.255.255.0") != NET_ERR_OK ||
        netif_set_addr(netif, &ip, &mask, 0) != NET_ERR_OK ||
        netif_set_active(netif) != NET_ERR_OK) {
        netif_close(netif);
        return NET_ERR_IO;
    }
    netif_set_default(netif);
    stack_netif = netif;
    stack_initialized = 1;
    return NET_ERR_OK;
}

netif_t *net_stack_default(void)
{
    return stack_netif;
}

net_err_t net_stack_process_input(netif_t *netif)
{
    if (netif == 0)
        return NET_ERR_PARAM;
    if (netif->state != NETIF_ACTIVE)
        return NET_ERR_STATE;
    pktbuf_t *buf = netif_get_in(netif, -1);
    if (buf == 0)
        return NET_ERR_NONE;

    net_err_t err;
    if (netif->type == NETIF_TYPE_LOOP)
        err = ipv4_in(netif, buf);
    else if (netif->type == NETIF_TYPE_ETHER && netif->link_layer != 0 &&
             netif->link_layer->in != 0)
        err = netif->link_layer->in(netif, buf);
    else
        err = NET_ERR_NOT_SUPPORT;
    if (err < 0)
        pktbuf_free(buf);
    return err;
}

net_err_t net_stack_poll_once(netif_t *netif, u64 deadline)
{
    if (netif == 0)
        return NET_ERR_PARAM;
    if (netif->state != NETIF_ACTIVE)
        return NET_ERR_STATE;
    net_err_t err = netif_virtio_poll(netif, deadline);
    if (err < 0)
        return err;
    return net_stack_process_input(netif);
}

void net_stack_worker(void *arg)
{
    netif_t *netif = (netif_t *)arg;
    net_time_t timer_time;

    if (netif == 0)
        netif = stack_netif;
    sys_time_curr(&timer_time);
    for (;;) {
        while (net_exec_run_once() == NET_ERR_OK)
            ;
        (void)net_timer_check_tmo(sys_time_goes(&timer_time));
#ifdef QS_M6A_TEST
        net_stack_queue_probe();
        net_stack_loop_probe();
#endif
        netif_t *loop = loop_get_netif();
        for (;;) {
            net_err_t err = net_stack_process_input(loop);
            if (err == NET_ERR_NONE)
                break;
            if (err < 0) {
#ifdef QS_M6A_TEST
                net_stack_probe_fail(9);
#else
                break;
#endif
            }
        }
#ifdef QS_M6A_TEST
        net_stack_loop_report();
#endif
#ifdef QS_M5_TEST
        net_stack_probe(netif);
#endif
        (void)net_timer_check_tmo(sys_time_goes(&timer_time));
        u64 wait_ticks = (u64)NET_STACK_RX_WAIT_MS *
                         NET_TIME_TICKS_PER_MS;
        int timer_ms = net_timer_first_tmo();
        if (timer_ms > 0 &&
            (u64)timer_ms * NET_TIME_TICKS_PER_MS < wait_ticks) {
            wait_ticks = (u64)timer_ms * NET_TIME_TICKS_PER_MS;
        }
        (void)net_stack_poll_once(netif, net_stack_now() + wait_ticks);
    }
}
