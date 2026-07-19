#include <timeros/net/udp.h>

#include <timeros/net/net_port.h>
#include <timeros/net/ipv4.h>
#include <timeros/net/pktbuf.h>
#include <timeros/net/protocol.h>
#include <timeros/net/tools.h>

typedef struct _udp_recv_t {
    pktbuf_t *buf;
    ipaddr_t src;
    uint16_t port;
} udp_recv_t;

static udp_recv_t recv_records[UDP_PCB_MAX][UDP_RECV_MAX];

typedef struct __attribute__((packed)) _udp_pseudo_t {
    uint8_t src[IPV4_ADDR_SIZE];
    uint8_t dest[IPV4_ADDR_SIZE];
    uint8_t zero;
    uint8_t protocol;
    uint16_t length;
} udp_pseudo_t;

static uint16_t udp_checksum(pktbuf_t *buf, const ipaddr_t *src,
                             const ipaddr_t *dest, uint16_t length)
{
    udp_pseudo_t pseudo;

    ipaddr_to_buf(src, pseudo.src);
    ipaddr_to_buf(dest, pseudo.dest);
    pseudo.zero = 0;
    pseudo.protocol = NET_PROTOCOL_UDP;
    pseudo.length = x_htons(length);
    uint32_t sum = checksum16(0, &pseudo, sizeof(pseudo), 0, 0);
    pktbuf_reset_acc(buf);
    return pktbuf_checksum16(buf, length, sum, 1);
}

static udp_pcb_t *pcbs[UDP_PCB_MAX];

net_err_t udp_init(void)
{
    plat_memset(pcbs, 0, sizeof(pcbs));
    return ipv4_register_handler(NET_PROTOCOL_UDP, udp_in);
}

static int udp_find_slot(udp_pcb_t *pcb)
{
    for (int i = 0; i < UDP_PCB_MAX; i++) {
        if (pcbs[i] == pcb)
            return i;
    }
    return -1;
}

net_err_t udp_open(udp_pcb_t *pcb)
{
    if (pcb == 0 || udp_find_slot(pcb) >= 0)
        return NET_ERR_PARAM;
    for (int i = 0; i < UDP_PCB_MAX; i++) {
        if (pcbs[i] == 0) {
            pcb->local_port = 0;
            pcb->open = 1;
            net_err_t err = fixq_init(&pcb->recv_queue, pcb->recv_slots,
                                      UDP_RECV_MAX, NLOCKER_THREAD);
            if (err < 0)
                return err;
            nlocker_init(&pcb->state_locker, NLOCKER_THREAD);
            pcb->close_done = sys_sem_create(0);
            if (pcb->close_done == SYS_SEM_INVALID) {
                fixq_destroy(&pcb->recv_queue);
                return NET_ERR_MEM;
            }
            pcb->recv_waiting = 0;
            pcbs[i] = pcb;
            return NET_ERR_OK;
        }
    }
    return NET_ERR_MEM;
}

net_err_t udp_bind(udp_pcb_t *pcb, uint16_t port)
{
    if (pcb == 0 || !pcb->open || udp_find_slot(pcb) < 0 || port == 0)
        return NET_ERR_PARAM;
    for (int i = 0; i < UDP_PCB_MAX; i++) {
        if (pcbs[i] != 0 && pcbs[i] != pcb &&
            pcbs[i]->open && pcbs[i]->local_port == port)
            return NET_ERR_EXIST;
    }
    pcb->local_port = port;
    return NET_ERR_OK;
}

net_err_t udp_close(udp_pcb_t *pcb)
{
    int slot = udp_find_slot(pcb);

    if (slot < 0 || pcb == 0 || !pcb->open)
        return NET_ERR_PARAM;
    nlocker_lock(&pcb->state_locker);
    pcb->open = 0;
    int waiting = pcb->recv_waiting;
    nlocker_unlock(&pcb->state_locker);
    if (waiting) {
        fixq_wake_receiver(&pcb->recv_queue);
        (void)sys_sem_wait(pcb->close_done, 0);
    }
    void *item;
    while ((item = fixq_recv(&pcb->recv_queue, -1)) != 0) {
        if (item == pcb)
            continue;
        udp_recv_t *record = item;
        pktbuf_free(record->buf);
        record->buf = 0;
    }
    fixq_destroy(&pcb->recv_queue);
    sys_sem_free(pcb->close_done);
    nlocker_destroy(&pcb->state_locker);
    pcb->local_port = 0;
    pcbs[slot] = 0;
    return NET_ERR_OK;
}

net_err_t udp_sendto(udp_pcb_t *pcb, netif_t *netif, const ipaddr_t *dest,
                     uint16_t dest_port, const uint8_t *data, int size)
{
    if (pcb == 0 || !pcb->open || netif == 0 || dest == 0 ||
        dest_port == 0 || size < 0 || (size != 0 && data == 0))
        return NET_ERR_PARAM;
    pktbuf_t *buf = pktbuf_alloc(UDP_HEADER_SIZE + size);
    if (buf == 0)
        return NET_ERR_MEM;
    udp_hdr_t *header = (udp_hdr_t *)pktbuf_data(buf);
    header->src_port = x_htons(pcb->local_port);
    header->dest_port = x_htons(dest_port);
    header->total_len = x_htons((uint16_t)(UDP_HEADER_SIZE + size));
    header->checksum = 0;
    if (size > 0) {
        pktbuf_reset_acc(buf);
        if (pktbuf_seek(buf, UDP_HEADER_SIZE) < 0 ||
            pktbuf_write(buf, data, size) < 0) {
            pktbuf_free(buf);
            return NET_ERR_SIZE;
        }
    }
    header = (udp_hdr_t *)pktbuf_data(buf);
    uint16_t checksum = udp_checksum(buf, &netif->ipaddr, dest,
                                     (uint16_t)(UDP_HEADER_SIZE + size));
    header->checksum = x_htons(checksum == 0 ? 0xffffU : checksum);
    return ipv4_out(netif, dest, NET_PROTOCOL_UDP, buf);
}

net_err_t udp_in(netif_t *netif, const ipaddr_t *src,
                 const ipaddr_t *dest, pktbuf_t *buf)
{
    (void)netif;
    (void)dest;
    if (src == 0 || buf == 0 || pktbuf_set_cont(buf, UDP_HEADER_SIZE) < 0)
        return NET_ERR_SIZE;
    udp_hdr_t *header = (udp_hdr_t *)pktbuf_data(buf);
    net_err_t err = udp_header_check(header, pktbuf_total(buf));
    if (err < 0)
        return err;
    uint16_t length = x_ntohs(header->total_len);
    if (header->checksum != 0 && udp_checksum(buf, src, dest, length) != 0)
        return NET_ERR_CHKSUM;
    if (pktbuf_resize(buf, length) < 0)
        return NET_ERR_SIZE;
    uint16_t port = x_ntohs(header->dest_port);
    for (int i = 0; i < UDP_PCB_MAX; i++) {
        udp_pcb_t *pcb = pcbs[i];
        if (pcb == 0 || !pcb->open || pcb->local_port != port)
            continue;
        for (int j = 0; j < UDP_RECV_MAX; j++) {
            udp_recv_t *record = &recv_records[i][j];
            if (record->buf != 0)
                continue;
            record->buf = buf;
            record->src = *src;
            record->port = x_ntohs(header->src_port);
            if (fixq_send(&pcb->recv_queue, record, -1) < 0) {
                record->buf = 0;
                return NET_ERR_FULL;
            }
            return NET_ERR_OK;
        }
        return NET_ERR_FULL;
    }
    return NET_ERR_UNREACH;
}

net_err_t udp_recv_acquire(udp_pcb_t *pcb)
{
    if (pcb == 0)
        return NET_ERR_PARAM;
    nlocker_lock(&pcb->state_locker);
    if (!pcb->open || pcb->recv_waiting) {
        nlocker_unlock(&pcb->state_locker);
        return NET_ERR_STATE;
    }
    pcb->recv_waiting = 1;
    nlocker_unlock(&pcb->state_locker);
    return NET_ERR_OK;
}

int udp_recvfrom_acquired(udp_pcb_t *pcb, uint8_t *data, int size,
                          ipaddr_t *src, uint16_t *src_port, int timeout_ms)
{
    if (pcb == 0 || data == 0 || size < 0)
        return NET_ERR_PARAM;
    void *item = fixq_recv(&pcb->recv_queue, timeout_ms);
    nlocker_lock(&pcb->state_locker);
    pcb->recv_waiting = 0;
    int closed = !pcb->open;
    nlocker_unlock(&pcb->state_locker);
    if (closed) {
        if (item != 0 && item != pcb) {
            udp_recv_t *record = item;
            pktbuf_free(record->buf);
            record->buf = 0;
        }
        sys_sem_notify(pcb->close_done);
        return NET_ERR_STATE;
    }
    udp_recv_t *record = item;
    if (record == 0)
        return timeout_ms < 0 ? NET_ERR_NONE : NET_ERR_TMO;
    if (pktbuf_remove_header(record->buf, UDP_HEADER_SIZE) < 0) {
        pktbuf_free(record->buf);
        record->buf = 0;
        return NET_ERR_SIZE;
    }
    int copied = pktbuf_total(record->buf);
    if (copied > size)
        copied = size;
    pktbuf_reset_acc(record->buf);
    if (pktbuf_read(record->buf, data, copied) < 0)
        copied = NET_ERR_SIZE;
    if (src != 0)
        *src = record->src;
    if (src_port != 0)
        *src_port = record->port;
    pktbuf_free(record->buf);
    record->buf = 0;
    return copied;
}

int udp_recvfrom(udp_pcb_t *pcb, uint8_t *data, int size, ipaddr_t *src,
                 uint16_t *src_port, int timeout_ms)
{
    if (pcb == 0 || data == 0 || size < 0)
        return NET_ERR_PARAM;
    if (udp_recv_acquire(pcb) < 0)
        return NET_ERR_STATE;
    return udp_recvfrom_acquired(pcb, data, size, src, src_port, timeout_ms);
}

net_err_t udp_header_check(const udp_hdr_t *header, int size)
{
    if (header == 0 || size < UDP_HEADER_SIZE)
        return NET_ERR_SIZE;
    if (x_ntohs(header->total_len) < UDP_HEADER_SIZE ||
        x_ntohs(header->total_len) > size)
        return NET_ERR_SIZE;
    return NET_ERR_OK;
}
