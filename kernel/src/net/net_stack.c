#include <timeros/net/net_stack.h>

#include <timeros/net/arp.h>
#include <timeros/net/ether.h>
#include <timeros/net/icmpv4.h>
#include <timeros/net/ipv4.h>
#include <timeros/net/netif_virtio.h>
#include <timeros/net/pktbuf.h>

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

static netif_t *stack_netif;
static int stack_initialized;

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
        (void)net_stack_poll_once(netif,
                                  net_stack_now() + NET_STACK_RX_DEADLINE);
    }
}
