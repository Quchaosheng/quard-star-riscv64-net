#include <timeros/net/net_stack.h>

#include <timeros/net/arp.h>
#include <timeros/net/ether.h>
#include <timeros/net/icmpv4.h>
#include <timeros/net/ipv4.h>
#include <timeros/net/netif_virtio.h>
#include <timeros/net/pktbuf.h>

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

#define NET_STACK_RX_DEADLINE 50000000ULL

#ifdef QS_M5_TEST
#define NET_STACK_PROBE_TIMEOUT 200000000ULL
#define NET_STACK_PROBE_IDENTIFIER 0x4d35
#define NET_STACK_PROBE_SEQUENCE 1
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
        asm volatile("wfi");
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
        probe_deadline = net_stack_now() + NET_STACK_PROBE_TIMEOUT;
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
    net_err_t err = pktbuf_init();
    if (err < 0)
        return err;
    if ((err = netif_init()) < 0 || (err = ether_init()) < 0 ||
        (err = arp_init()) < 0 || (err = ipv4_init()) < 0 ||
        (err = icmpv4_init()) < 0)
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

net_err_t net_stack_poll_once(netif_t *netif, u64 deadline)
{
    if (netif == 0)
        return NET_ERR_PARAM;
    if (netif->state != NETIF_ACTIVE)
        return NET_ERR_STATE;
    net_err_t err = netif_virtio_poll(netif, deadline);
    if (err < 0)
        return err;
    pktbuf_t *buf = netif_get_in(netif, -1);
    if (buf == 0)
        return NET_ERR_SYS;
    if (netif->link_layer == 0 || netif->link_layer->in == 0) {
        pktbuf_free(buf);
        return NET_ERR_NOT_SUPPORT;
    }
    err = netif->link_layer->in(netif, buf);
    if (err < 0)
        pktbuf_free(buf);
    return err;
}

void net_stack_worker(void *arg)
{
    netif_t *netif = (netif_t *)arg;
    if (netif == 0)
        netif = stack_netif;
    for (;;) {
#ifdef QS_M5_TEST
        net_stack_probe(netif);
#endif
        (void)net_stack_poll_once(netif,
                                  net_stack_now() + NET_STACK_RX_DEADLINE);
    }
}
