#include <timeros/net/arp.h>

#include <timeros/net/mblock.h>
#include <timeros/net/net_cfg.h>
#include <timeros/net/net_port.h>
#include <timeros/net/protocol.h>
#include <timeros/net/timer.h>
#include <timeros/net/tools.h>

#ifdef QS_M6A_TEST
#include <timeros/selftest.h>
extern int printk(const char *format, ...);
#endif

static net_timer_t cache_timer;
static arp_entry_t cache_entries[ARP_CACHE_SIZE];
static mblock_t cache_blocks;
static nlist_t cache_list;
static const uint8_t empty_hwaddr[ETH_HWA_SIZE];
static int initialized;

static int arp_scan_count(int seconds)
{
    return 1 + (seconds - 1) / ARP_TIMER_TMO;
}

static void arp_free_waiting(arp_entry_t *entry)
{
    nlist_node_t *node;

    while ((node = nlist_remove_first(&entry->buf_list)) != 0) {
        pktbuf_t *buf = nlist_entry(node, pktbuf_t, node);
        pktbuf_free(buf);
    }
}

static arp_entry_t *arp_entry_from_node(nlist_node_t *node)
{
    return nlist_entry(node, arp_entry_t, node);
}

static arp_entry_t *arp_find_entry(netif_t *netif, const ipaddr_t *ipaddr)
{
    nlist_node_t *node;

    nlist_for_each(node, &cache_list) {
        arp_entry_t *entry = arp_entry_from_node(node);

        if (entry->netif == netif && ipaddr_is_equal(&entry->paddr, ipaddr)) {
            nlist_remove(&cache_list, node);
            nlist_insert_first(&cache_list, node);
            return entry;
        }
    }
    return 0;
}

static arp_entry_t *arp_alloc_entry(int force)
{
    arp_entry_t *entry = (arp_entry_t *)mblock_alloc(&cache_blocks, -1);

    if (entry == 0 && force) {
        nlist_node_t *node = nlist_remove_last(&cache_list);
        if (node != 0) {
            entry = arp_entry_from_node(node);
            arp_free_waiting(entry);
        }
    }
    if (entry != 0) {
        plat_memset(entry, 0, sizeof(*entry));
        nlist_node_init(&entry->node);
        nlist_init(&entry->buf_list);
        entry->state = NET_ARP_FREE;
    }
    return entry;
}

static void arp_free_entry(arp_entry_t *entry)
{
    if (entry == 0)
        return;
    arp_free_waiting(entry);
    if (entry->state != NET_ARP_FREE)
        nlist_remove(&cache_list, &entry->node);
    entry->state = NET_ARP_FREE;
    mblock_free(&cache_blocks, entry);
}

static void arp_set_entry(arp_entry_t *entry, netif_t *netif,
                          const uint8_t *ipbuf, const uint8_t *mac,
                          arp_state_t state)
{
    entry->netif = netif;
    ipaddr_from_buf(&entry->paddr, ipbuf);
    plat_memcpy(entry->haddr, mac, ETH_HWA_SIZE);
    entry->state = state;
    entry->tmo = arp_scan_count(state == NET_ARP_RESOLVED
                                    ? ARP_ENTRY_STABLE_TMO
                                    : ARP_ENTRY_PENDING_TMO);
    entry->retry = ARP_ENTRY_RETRY_CNT;
}

static void arp_cache_tmo(net_timer_t *timer, void *arg)
{
    nlist_node_t *node = nlist_first(&cache_list);

#ifdef QS_M6A_TEST
    static int reported;
    if (!reported) {
        reported = 1;
        printk("\nQS:M6_ARP_TIMER_OK\n");
        m6_mark_arp_timer();
    }
#endif

    (void)timer;
    (void)arg;
    while (node != 0) {
        nlist_node_t *next = nlist_node_next(node);
        arp_entry_t *entry = arp_entry_from_node(node);

        if (--entry->tmo > 0) {
            node = next;
            continue;
        }
        if (entry->state == NET_ARP_RESOLVED) {
            ipaddr_t ipaddr = entry->paddr;

            entry->state = NET_ARP_WAITING;
            entry->tmo = arp_scan_count(ARP_ENTRY_PENDING_TMO);
            entry->retry = ARP_ENTRY_RETRY_CNT;
            (void)arp_make_request(entry->netif, &ipaddr);
        } else if (entry->state == NET_ARP_WAITING) {
            if (--entry->retry <= 0) {
                arp_free_entry(entry);
            } else {
                ipaddr_t ipaddr = entry->paddr;

                entry->tmo = arp_scan_count(ARP_ENTRY_PENDING_TMO);
                (void)arp_make_request(entry->netif, &ipaddr);
            }
        }
        node = next;
    }
}

static net_err_t arp_send_waiting(arp_entry_t *entry)
{
    nlist_node_t *node;

    while ((node = nlist_remove_first(&entry->buf_list)) != 0) {
        pktbuf_t *buf = nlist_entry(node, pktbuf_t, node);
        net_err_t err = ether_raw_out(entry->netif, NET_PROTOCOL_IPV4,
                                      entry->haddr, buf);
        if (err < 0) {
            arp_free_waiting(entry);
            return err;
        }
    }
    return NET_ERR_OK;
}

static net_err_t arp_cache_insert(netif_t *netif, const uint8_t *ipbuf,
                                  const uint8_t *mac, int force)
{
    ipaddr_t ip;

    ipaddr_from_buf(&ip, ipbuf);
    arp_entry_t *entry = arp_find_entry(netif, &ip);
    if (entry == 0) {
        entry = arp_alloc_entry(force);
        if (entry == 0)
            return NET_ERR_MEM;
        arp_set_entry(entry, netif, ipbuf, mac, NET_ARP_RESOLVED);
        nlist_insert_first(&cache_list, &entry->node);
    } else {
        arp_set_entry(entry, netif, ipbuf, mac, NET_ARP_RESOLVED);
    }
    return arp_send_waiting(entry);
}

static net_err_t arp_packet_check(const arp_pkt_t *packet, int size)
{
    if (packet == 0 || size < (int)sizeof(arp_pkt_t))
        return NET_ERR_SIZE;
    if (x_ntohs(packet->htype) != ARP_HW_ETHER ||
        packet->hlen != ETH_HWA_SIZE || packet->plen != IPV4_ADDR_SIZE ||
        x_ntohs(packet->ptype) != NET_PROTOCOL_IPV4)
        return NET_ERR_NOT_SUPPORT;
    uint16_t opcode = x_ntohs(packet->opcode);
    if (opcode != ARP_REQUEST && opcode != ARP_REPLY)
        return NET_ERR_NOT_SUPPORT;
    return NET_ERR_OK;
}

net_err_t arp_init(void)
{
    if (initialized)
        return NET_ERR_EXIST;
    nlist_init(&cache_list);
    net_err_t err = mblock_init(&cache_blocks, cache_entries,
                                sizeof(cache_entries[0]), ARP_CACHE_SIZE,
                                NLOCKER_NONE);
    if (err < 0)
        return err;
    err = net_timer_add(&cache_timer, "arp timer", arp_cache_tmo, 0,
                        ARP_TIMER_TMO * 1000, NET_TIMER_RELOAD);
    if (err < 0)
        return err;
    err = ether_register_handler(NET_PROTOCOL_ARP, arp_in);
    if (err < 0) {
        net_timer_remove(&cache_timer);
        return err;
    }
    initialized = 1;
    return NET_ERR_OK;
}

net_err_t arp_make_request(netif_t *netif, const ipaddr_t *protocol_addr)
{
    if (netif == 0 || protocol_addr == 0)
        return NET_ERR_PARAM;
    pktbuf_t *buf = pktbuf_alloc(sizeof(arp_pkt_t));
    if (buf == 0)
        return NET_ERR_MEM;
    pktbuf_reset_acc(buf);
    arp_pkt_t *packet = (arp_pkt_t *)pktbuf_data(buf);
    if (packet == 0) {
        pktbuf_free(buf);
        return NET_ERR_MEM;
    }
    packet->htype = x_htons(ARP_HW_ETHER);
    packet->ptype = x_htons(NET_PROTOCOL_IPV4);
    packet->hlen = ETH_HWA_SIZE;
    packet->plen = IPV4_ADDR_SIZE;
    packet->opcode = x_htons(ARP_REQUEST);
    plat_memcpy(packet->send_haddr, netif->hwaddr.addr, ETH_HWA_SIZE);
    ipaddr_to_buf(&netif->ipaddr, packet->send_paddr);
    plat_memcpy(packet->target_haddr, empty_hwaddr, ETH_HWA_SIZE);
    ipaddr_to_buf(protocol_addr, packet->target_paddr);

    net_err_t err = ether_raw_out(netif, NET_PROTOCOL_ARP,
                                  ether_broadcast_addr(), buf);
    return err;
}

net_err_t arp_make_gratuitous(netif_t *netif)
{
    if (netif == 0)
        return NET_ERR_PARAM;
    return arp_make_request(netif, &netif->ipaddr);
}

net_err_t arp_make_reply(netif_t *netif, pktbuf_t *buf)
{
    if (netif == 0 || buf == 0)
        return NET_ERR_PARAM;
    if (pktbuf_set_cont(buf, sizeof(arp_pkt_t)) != NET_ERR_OK)
        return NET_ERR_SIZE;
    arp_pkt_t *packet = (arp_pkt_t *)pktbuf_data(buf);
    if (packet == 0)
        return NET_ERR_SIZE;
    uint8_t target_mac[ETH_HWA_SIZE];
    uint8_t target_ip[IPV4_ADDR_SIZE];
    plat_memcpy(target_mac, packet->send_haddr, ETH_HWA_SIZE);
    plat_memcpy(target_ip, packet->send_paddr, IPV4_ADDR_SIZE);
    packet->opcode = x_htons(ARP_REPLY);
    plat_memcpy(packet->target_haddr, target_mac, ETH_HWA_SIZE);
    plat_memcpy(packet->target_paddr, target_ip, IPV4_ADDR_SIZE);
    plat_memcpy(packet->send_haddr, netif->hwaddr.addr, ETH_HWA_SIZE);
    ipaddr_to_buf(&netif->ipaddr, packet->send_paddr);
    pktbuf_inc_ref(buf);
    net_err_t err = ether_raw_out(netif, NET_PROTOCOL_ARP, target_mac, buf);
    if (err >= 0)
        pktbuf_free(buf);
    return err;
}

net_err_t arp_in(netif_t *netif, pktbuf_t *buf)
{
    if (netif == 0 || buf == 0)
        return NET_ERR_PARAM;
    if (pktbuf_set_cont(buf, sizeof(arp_pkt_t)) != NET_ERR_OK)
        return NET_ERR_SIZE;
    arp_pkt_t *packet = (arp_pkt_t *)pktbuf_data(buf);
    if (packet == 0)
        return NET_ERR_SIZE;
    net_err_t err = arp_packet_check(packet, pktbuf_total(buf));
    if (err < 0)
        return err;

    uint16_t opcode = x_ntohs(packet->opcode);
    err = arp_cache_insert(netif, packet->send_paddr, packet->send_haddr,
                           opcode == ARP_REQUEST);
    if (err < 0)
        return err;

    ipaddr_t target;
    ipaddr_from_buf(&target, packet->target_paddr);
    if (opcode == ARP_REQUEST && ipaddr_is_equal(&target, &netif->ipaddr)) {
        err = arp_make_reply(netif, buf);
        return err;
    }
    pktbuf_free(buf);
    return NET_ERR_OK;
}

const uint8_t *arp_find(netif_t *netif, const ipaddr_t *ipaddr)
{
    if (netif == 0 || ipaddr == 0)
        return 0;
    if (ipaddr_is_local_broadcast(ipaddr) ||
        ipaddr_is_direct_broadcast(ipaddr, &netif->netmask))
        return ether_broadcast_addr();
    arp_entry_t *entry = arp_find_entry(netif, ipaddr);
    if (entry != 0 && entry->state == NET_ARP_RESOLVED)
        return entry->haddr;
    return 0;
}

net_err_t arp_resolve(netif_t *netif, const ipaddr_t *ipaddr,
                      pktbuf_t *buf)
{
    if (buf == 0)
        return NET_ERR_PARAM;
    if (netif == 0 || ipaddr == 0) {
        pktbuf_free(buf);
        return NET_ERR_PARAM;
    }
    const uint8_t *broadcast = arp_find(netif, ipaddr);
    if (broadcast == ether_broadcast_addr())
        return ether_raw_out(netif, NET_PROTOCOL_IPV4, broadcast, buf);

    arp_entry_t *entry = arp_find_entry(netif, ipaddr);
    if (entry != 0 && entry->state == NET_ARP_RESOLVED)
        return ether_raw_out(netif, NET_PROTOCOL_IPV4, entry->haddr, buf);
    if (entry != 0) {
        if (nlist_count(&entry->buf_list) >= ARP_MAX_PKT_WAIT) {
            pktbuf_free(buf);
            return NET_ERR_FULL;
        }
        nlist_insert_last(&entry->buf_list, &buf->node);
        return NET_ERR_OK;
    }

    entry = arp_alloc_entry(1);
    if (entry == 0) {
        pktbuf_free(buf);
        return NET_ERR_MEM;
    }
    uint8_t ipbuf[IPV4_ADDR_SIZE];
    ipaddr_to_buf(ipaddr, ipbuf);
    arp_set_entry(entry, netif, ipbuf, empty_hwaddr, NET_ARP_WAITING);
    nlist_insert_first(&cache_list, &entry->node);
    nlist_insert_last(&entry->buf_list, &buf->node);
    net_err_t err = arp_make_request(netif, ipaddr);
    if (err < 0) {
        arp_free_entry(entry);
        return err;
    }
    return NET_ERR_OK;
}

void arp_clear(netif_t *netif)
{
    if (!initialized || netif == 0)
        return;
    nlist_node_t *node = nlist_first(&cache_list);
    while (node != 0) {
        nlist_node_t *next = nlist_node_next(node);
        arp_entry_t *entry = arp_entry_from_node(node);
        if (entry->netif == netif)
            arp_free_entry(entry);
        node = next;
    }
}
