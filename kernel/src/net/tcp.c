#include <timeros/net/tcp.h>

#include <timeros/net/ipv4.h>
#include <timeros/net/net_port.h>
#include <timeros/net/protocol.h>
#include <timeros/net/tools.h>

typedef struct __attribute__((packed)) _tcp_pseudo_t {
    uint8_t src[IPV4_ADDR_SIZE];
    uint8_t dest[IPV4_ADDR_SIZE];
    uint8_t zero;
    uint8_t protocol;
    uint16_t length;
} tcp_pseudo_t;

static tcp_pcb_t *pcbs[TCP_PCB_MAX];
static uint16_t next_ephemeral = 49152;
static uint32_t next_iss = 1000;

static net_err_t tcp_release_now(tcp_pcb_t *pcb);

static int tcp_find_slot(tcp_pcb_t *pcb)
{
    for (int i = 0; i < TCP_PCB_MAX; i++) {
        if (pcbs[i] == pcb)
            return i;
    }
    return -1;
}

static pktbuf_t *tcp_clone(pktbuf_t *source)
{
    int size = pktbuf_total(source);
    pktbuf_t *copy = pktbuf_alloc(size);

    if (copy == 0)
        return 0;
    pktbuf_reset_acc(source);
    pktbuf_reset_acc(copy);
    if (pktbuf_copy(copy, source, size) < 0) {
        pktbuf_free(copy);
        pktbuf_reset_acc(source);
        return 0;
    }
    pktbuf_reset_acc(source);
    pktbuf_reset_acc(copy);
    return copy;
}

static net_err_t tcp_output_owned(tcp_pcb_t *pcb)
{
    pktbuf_t *copy = tcp_clone(pcb->outstanding);

    if (copy == 0)
        return NET_ERR_MEM;
    return ipv4_out(pcb->netif, &pcb->remote_ip, NET_PROTOCOL_TCP, copy);
}

static pktbuf_t *tcp_make_segment(tcp_pcb_t *pcb, uint32_t seq,
                                  uint32_t ack, uint8_t flags,
                                  const uint8_t *data, int size)
{
    pktbuf_t *buf = pktbuf_alloc(TCP_HEADER_SIZE + size);

    if (buf == 0)
        return 0;
    tcp_hdr_t *header = (tcp_hdr_t *)pktbuf_data(buf);
    plat_memset(header, 0, sizeof(*header));
    header->src_port = x_htons(pcb->local_port);
    header->dest_port = x_htons(pcb->remote_port);
    header->seq = x_htonl(seq);
    header->ack = x_htonl(ack);
    header->data_offset = 5U << 4;
    header->flags = flags;
    nlocker_lock(&pcb->recv_locker);
    int available = TCP_RECV_MAX - pcb->recv_count;
    nlocker_unlock(&pcb->recv_locker);
    header->window = x_htons((uint16_t)available);
    if (size > 0) {
        pktbuf_reset_acc(buf);
        if (pktbuf_seek(buf, TCP_HEADER_SIZE) < 0 ||
            pktbuf_write(buf, data, size) < 0) {
            pktbuf_free(buf);
            return 0;
        }
    }
    header = (tcp_hdr_t *)pktbuf_data(buf);
    uint16_t checksum = tcp_checksum(buf, &pcb->local_ip,
                                     &pcb->remote_ip,
                                     (uint16_t)(TCP_HEADER_SIZE + size));
    header->checksum = x_htons(checksum == 0 ? 0xffffU : checksum);
    pktbuf_reset_acc(buf);
    return buf;
}

static net_err_t tcp_send_ack(tcp_pcb_t *pcb)
{
    pktbuf_t *ack = tcp_make_segment(pcb, pcb->snd_nxt, pcb->rcv_nxt,
                                     TCP_FLAG_ACK, 0, 0);

    if (ack == 0)
        return NET_ERR_MEM;
    return ipv4_out(pcb->netif, &pcb->remote_ip, NET_PROTOCOL_TCP, ack);
}

static void tcp_clear_outstanding(tcp_pcb_t *pcb)
{
    net_timer_remove(&pcb->retrans_timer);
    if (pcb->outstanding != 0) {
        pktbuf_free(pcb->outstanding);
        pcb->outstanding = 0;
    }
    pcb->outstanding_end = 0;
}

static void tcp_notify_terminal(tcp_pcb_t *pcb)
{
    nlocker_lock(&pcb->state_locker);
    if (pcb->terminal_notified) {
        nlocker_unlock(&pcb->state_locker);
        return;
    }
    pcb->terminal_notified = 1;
    int connect_waiters = pcb->connect_waiters;
    int recv_waiters = pcb->recv_waiters;
    int close_waiters = pcb->close_waiters;
    nlocker_unlock(&pcb->state_locker);
    for (int i = 0; i < (connect_waiters != 0 ? connect_waiters : 1); i++)
        sys_sem_notify(pcb->connect_done);
    for (int i = 0; i < recv_waiters; i++)
        sys_sem_notify(pcb->recv_done);
    for (int i = 0; i < (close_waiters != 0 ? close_waiters : 1); i++)
        sys_sem_notify(pcb->close_done);
}

static net_err_t tcp_fail(tcp_pcb_t *pcb, net_err_t error)
{
    tcp_clear_outstanding(pcb);
    net_timer_remove(&pcb->time_wait_timer);
    nlocker_lock(&pcb->state_locker);
    pcb->error = error;
    pcb->state = TCP_STATE_CLOSED;
    nlocker_unlock(&pcb->state_locker);
    tcp_notify_terminal(pcb);
    return error;
}

static void tcp_timer_proc(net_timer_t *timer, void *arg)
{
    (void)timer;
    (void)tcp_retransmit_due((tcp_pcb_t *)arg);
}

static void tcp_time_wait_proc(net_timer_t *timer, void *arg)
{
    (void)timer;
    tcp_pcb_t *pcb = (tcp_pcb_t *)arg;

    nlocker_lock(&pcb->state_locker);
    int expires = pcb->opened && pcb->state == TCP_STATE_TIME_WAIT &&
                  !pcb->release_pending;
    if (expires) {
        pcb->state = TCP_STATE_CLOSED;
        pcb->error = NET_ERR_OK;
    }
    nlocker_unlock(&pcb->state_locker);
    if (expires) {
        nlocker_lock(&pcb->state_locker);
        int close_waiters = pcb->close_waiters;
        nlocker_unlock(&pcb->state_locker);
        for (int i = 0; i < (close_waiters != 0 ? close_waiters : 1); i++)
            sys_sem_notify(pcb->close_done);
    }
}

static net_err_t tcp_arm_timer(tcp_pcb_t *pcb)
{
    return net_timer_add(&pcb->retrans_timer, "tcp-retrans",
                         tcp_timer_proc, pcb, TCP_RETRANS_MS, 0);
}

static int tcp_port_in_use(uint16_t port)
{
    for (int i = 0; i < TCP_PCB_MAX; i++) {
        if (pcbs[i] != 0 && pcbs[i]->opened &&
            pcbs[i]->local_port == port)
            return 1;
    }
    return 0;
}

static uint16_t tcp_choose_port(void)
{
    for (int i = 0; i < 16384; i++) {
        uint16_t port = next_ephemeral++;

        if (next_ephemeral < 49152)
            next_ephemeral = 49152;
        if (port != 0 && !tcp_port_in_use(port))
            return port;
    }
    return 0;
}

static void tcp_drain_sem(sys_sem_t sem)
{
    while (sys_sem_wait(sem, -1) == NET_ERR_OK)
        ;
}

static void tcp_reset_connection(tcp_pcb_t *pcb)
{
    pcb->netif = 0;
    ipaddr_set_any(&pcb->local_ip);
    ipaddr_set_any(&pcb->remote_ip);
    pcb->local_port = 0;
    pcb->remote_port = 0;
    pcb->state = TCP_STATE_CLOSED;
    pcb->iss = 0;
    pcb->snd_una = 0;
    pcb->snd_nxt = 0;
    pcb->rcv_nxt = 0;
    pcb->window = 0;
    pcb->outstanding_end = 0;
    pcb->retry_count = 0;
}

static net_err_t tcp_start_outstanding(tcp_pcb_t *pcb, pktbuf_t *segment,
                                       uint32_t end)
{
    pcb->outstanding = segment;
    pcb->outstanding_end = end;
    pcb->retry_count = 0;
    net_err_t err = tcp_output_owned(pcb);
    if (err >= 0)
        err = tcp_arm_timer(pcb);
    if (err < 0)
        tcp_clear_outstanding(pcb);
    return err;
}

net_err_t tcp_init(void)
{
    plat_memset(pcbs, 0, sizeof(pcbs));
    next_ephemeral = 49152;
    next_iss = 1000;
    return ipv4_register_handler(NET_PROTOCOL_TCP, tcp_in);
}

net_err_t tcp_open(tcp_pcb_t *pcb)
{
    if (pcb == 0 || tcp_find_slot(pcb) >= 0)
        return NET_ERR_PARAM;
    int slot = -1;
    for (int i = 0; i < TCP_PCB_MAX; i++) {
        if (pcbs[i] == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return NET_ERR_MEM;

    plat_memset(pcb, 0, sizeof(*pcb));
    nlocker_init(&pcb->recv_locker, NLOCKER_THREAD);
    nlocker_init(&pcb->state_locker, NLOCKER_THREAD);
    pcb->connect_done = sys_sem_create(0);
    if (pcb->connect_done == SYS_SEM_INVALID)
        goto fail;
    pcb->recv_done = sys_sem_create(0);
    if (pcb->recv_done == SYS_SEM_INVALID)
        goto fail;
    pcb->close_done = sys_sem_create(0);
    if (pcb->close_done == SYS_SEM_INVALID)
        goto fail;
    pcb->opened = 1;
    pcb->state = TCP_STATE_CLOSED;
    pcb->error = NET_ERR_OK;
    pcbs[slot] = pcb;
    return NET_ERR_OK;

fail:
    sys_sem_free(pcb->close_done);
    sys_sem_free(pcb->recv_done);
    sys_sem_free(pcb->connect_done);
    nlocker_destroy(&pcb->recv_locker);
    nlocker_destroy(&pcb->state_locker);
    plat_memset(pcb, 0, sizeof(*pcb));
    return NET_ERR_MEM;
}

net_err_t tcp_connect_start(tcp_pcb_t *pcb, netif_t *netif,
                            const ipaddr_t *remote, uint16_t remote_port)
{
    if (pcb == 0 || netif == 0 || remote == 0 || remote_port == 0)
        return NET_ERR_PARAM;
    if (tcp_find_slot(pcb) < 0)
        return NET_ERR_STATE;
    nlocker_lock(&pcb->state_locker);
    int unavailable = !pcb->opened || pcb->release_pending ||
                      pcb->connect_waiters != 0 ||
                      pcb->recv_waiters != 0 || pcb->close_waiters != 0 ||
                      pcb->state != TCP_STATE_CLOSED ||
                      pcb->outstanding != 0;
    nlocker_unlock(&pcb->state_locker);
    if (unavailable)
        return NET_ERR_STATE;
    if (netif->state != NETIF_ACTIVE)
        return NET_ERR_STATE;
    uint16_t local_port = tcp_choose_port();
    if (local_port == 0)
        return NET_ERR_FULL;

    tcp_drain_sem(pcb->connect_done);
    tcp_drain_sem(pcb->recv_done);
    tcp_drain_sem(pcb->close_done);
    nlocker_lock(&pcb->recv_locker);
    pcb->recv_head = 0;
    pcb->recv_count = 0;
    nlocker_unlock(&pcb->recv_locker);
    tcp_reset_connection(pcb);
    pcb->error = NET_ERR_OK;
    pcb->terminal_notified = 0;
    pcb->netif = netif;
    ipaddr_copy(&pcb->local_ip, &netif->ipaddr);
    ipaddr_copy(&pcb->remote_ip, remote);
    pcb->local_port = local_port;
    pcb->remote_port = remote_port;
    pcb->iss = next_iss;
    next_iss += 64000U;
    pcb->snd_una = pcb->iss;
    pcb->snd_nxt = pcb->iss + 1U;
    pcb->rcv_nxt = 0;
    pktbuf_t *syn = tcp_make_segment(pcb, pcb->iss, 0,
                                     TCP_FLAG_SYN, 0, 0);
    if (syn == 0) {
        tcp_reset_connection(pcb);
        return NET_ERR_MEM;
    }
    net_err_t err = tcp_start_outstanding(pcb, syn, pcb->snd_nxt);
    if (err < 0) {
        tcp_reset_connection(pcb);
        return err;
    }
    pcb->state = TCP_STATE_SYN_SENT;
    return NET_ERR_OK;
}

net_err_t tcp_send_start(tcp_pcb_t *pcb, const uint8_t *data, int size)
{
    if (pcb == 0 || data == 0 || size <= 0 || size > TCP_MSS)
        return NET_ERR_PARAM;
    if (!pcb->opened || pcb->state != TCP_STATE_ESTABLISHED)
        return NET_ERR_STATE;
    if (pcb->outstanding != 0)
        return NET_ERR_FULL;
    if (pcb->window == 0 || pcb->window < (uint16_t)size)
        return NET_ERR_FULL;

    uint32_t end = pcb->snd_nxt + (uint32_t)size;
    pktbuf_t *segment = tcp_make_segment(pcb, pcb->snd_nxt, pcb->rcv_nxt,
                                         TCP_FLAG_PSH | TCP_FLAG_ACK,
                                         data, size);
    if (segment == 0)
        return NET_ERR_MEM;
    net_err_t err = tcp_start_outstanding(pcb, segment, end);
    if (err < 0)
        return err;
    pcb->snd_nxt = end;
    return NET_ERR_OK;
}

static net_err_t tcp_wait_completion(tcp_pcb_t *pcb, sys_sem_t sem,
                                     int *waiters, int timeout_ms)
{
    if (pcb == 0 || tcp_find_slot(pcb) < 0)
        return NET_ERR_PARAM;
    nlocker_lock(&pcb->state_locker);
    if (!pcb->opened || pcb->release_pending) {
        nlocker_unlock(&pcb->state_locker);
        return NET_ERR_STATE;
    }
    if (pcb->error < 0) {
        net_err_t terminal_error = pcb->error;
        nlocker_unlock(&pcb->state_locker);
        return terminal_error;
    }
    (*waiters)++;
    nlocker_unlock(&pcb->state_locker);

    net_err_t err = sys_sem_wait(sem, timeout_ms);
    nlocker_lock(&pcb->state_locker);
    if (err == NET_ERR_OK && pcb->error < 0)
        err = pcb->error;
    (*waiters)--;
    int release = pcb->connect_waiters == 0 &&
                  pcb->recv_waiters == 0 &&
                  pcb->close_waiters == 0 && pcb->release_pending;
    if (release)
        pcb->opened = 0;
    nlocker_unlock(&pcb->state_locker);
    if (release)
        (void)tcp_release_now(pcb);
    if (timeout_ms < 0 && err == NET_ERR_TMO)
        return NET_ERR_NONE;
    return err;
}

net_err_t tcp_wait_connect(tcp_pcb_t *pcb, int timeout_ms)
{
    if (pcb == 0)
        return NET_ERR_PARAM;
    return tcp_wait_completion(pcb, pcb->connect_done,
                               &pcb->connect_waiters, timeout_ms);
}

net_err_t tcp_wait_close(tcp_pcb_t *pcb, int timeout_ms)
{
    if (pcb == 0)
        return NET_ERR_PARAM;
    return tcp_wait_completion(pcb, pcb->close_done,
                               &pcb->close_waiters, timeout_ms);
}

int tcp_recv_bytes(tcp_pcb_t *pcb, uint8_t *data, int size,
                   int timeout_ms)
{
    if (pcb == 0 || data == 0 || size <= 0)
        return NET_ERR_PARAM;
    nlocker_lock(&pcb->state_locker);
    if (!pcb->opened || pcb->release_pending) {
        nlocker_unlock(&pcb->state_locker);
        return NET_ERR_STATE;
    }
    if (pcb->state == TCP_STATE_CLOSED ||
        pcb->state == TCP_STATE_TIME_WAIT) {
        net_err_t state_error = pcb->error < 0 ? pcb->error : NET_ERR_STATE;
        nlocker_unlock(&pcb->state_locker);
        return state_error;
    }
    pcb->recv_waiters++;
    nlocker_unlock(&pcb->state_locker);

    int result;
    for (;;) {
        nlocker_lock(&pcb->recv_locker);
        if (pcb->recv_count != 0) {
            (void)sys_sem_wait(pcb->recv_done, -1);
            int copied = size < pcb->recv_count ? size : pcb->recv_count;
            int first = TCP_RECV_MAX - pcb->recv_head;
            if (first > copied)
                first = copied;
            plat_memcpy(data, pcb->recv_storage + pcb->recv_head,
                        (size_t)first);
            if (first < copied)
                plat_memcpy(data + first, pcb->recv_storage,
                            (size_t)(copied - first));
            pcb->recv_head = (pcb->recv_head + copied) % TCP_RECV_MAX;
            pcb->recv_count -= copied;
            if (pcb->recv_count != 0)
                sys_sem_notify(pcb->recv_done);
            nlocker_unlock(&pcb->recv_locker);
            result = copied;
            break;
        }
        nlocker_unlock(&pcb->recv_locker);

        nlocker_lock(&pcb->state_locker);
        int terminal = pcb->error < 0 || pcb->release_pending ||
                       !pcb->opened || pcb->state == TCP_STATE_CLOSED ||
                       pcb->state == TCP_STATE_TIME_WAIT;
        net_err_t terminal_error = pcb->error < 0 ? pcb->error :
                                   NET_ERR_STATE;
        nlocker_unlock(&pcb->state_locker);
        if (terminal) {
            (void)sys_sem_wait(pcb->recv_done, -1);
            result = terminal_error;
            break;
        }

        net_err_t err = sys_sem_wait(pcb->recv_done, timeout_ms);
        if (err < 0) {
            result = timeout_ms < 0 && err == NET_ERR_TMO ?
                     NET_ERR_NONE : err;
            break;
        }
    }

    nlocker_lock(&pcb->state_locker);
    pcb->recv_waiters--;
    int release = pcb->connect_waiters == 0 &&
                  pcb->recv_waiters == 0 &&
                  pcb->close_waiters == 0 && pcb->release_pending;
    if (release)
        pcb->opened = 0;
    nlocker_unlock(&pcb->state_locker);
    if (release)
        (void)tcp_release_now(pcb);
    return result;
}

net_err_t tcp_retransmit_due(tcp_pcb_t *pcb)
{
    if (pcb == 0 || !pcb->opened || pcb->outstanding == 0)
        return NET_ERR_STATE;
    net_timer_remove(&pcb->retrans_timer);
    net_err_t err = tcp_output_owned(pcb);
    if (err < 0)
        return tcp_fail(pcb, err);
    pcb->retry_count++;
    if (pcb->retry_count >= TCP_RETRY_MAX)
        return tcp_fail(pcb, NET_ERR_TMO);
    err = tcp_arm_timer(pcb);
    if (err < 0)
        return tcp_fail(pcb, err);
    return NET_ERR_OK;
}

static net_err_t tcp_release_now(tcp_pcb_t *pcb)
{
    int slot = tcp_find_slot(pcb);

    if (pcb == 0 || slot < 0)
        return NET_ERR_PARAM;
    tcp_clear_outstanding(pcb);
    net_timer_remove(&pcb->time_wait_timer);
    pcbs[slot] = 0;
    sys_sem_free(pcb->close_done);
    sys_sem_free(pcb->recv_done);
    sys_sem_free(pcb->connect_done);
    pcb->close_done = SYS_SEM_INVALID;
    pcb->recv_done = SYS_SEM_INVALID;
    pcb->connect_done = SYS_SEM_INVALID;
    nlocker_destroy(&pcb->recv_locker);
    nlocker_destroy(&pcb->state_locker);
    pcb->opened = 0;
    pcb->release_pending = 0;
    pcb->connect_waiters = 0;
    pcb->recv_waiters = 0;
    pcb->close_waiters = 0;
    pcb->recv_head = 0;
    pcb->recv_count = 0;
    tcp_reset_connection(pcb);
    return NET_ERR_OK;
}

static net_err_t tcp_request_release(tcp_pcb_t *pcb)
{
    nlocker_lock(&pcb->state_locker);
    pcb->release_pending = 1;
    int release = pcb->connect_waiters == 0 &&
                  pcb->recv_waiters == 0 && pcb->close_waiters == 0;
    if (release)
        pcb->opened = 0;
    nlocker_unlock(&pcb->state_locker);
    return release ? tcp_release_now(pcb) : NET_ERR_OK;
}

static net_err_t tcp_abort_and_release(tcp_pcb_t *pcb)
{
    tcp_clear_outstanding(pcb);
    net_timer_remove(&pcb->time_wait_timer);
    nlocker_lock(&pcb->state_locker);
    pcb->error = NET_ERR_STATE;
    pcb->state = TCP_STATE_CLOSED;
    nlocker_unlock(&pcb->state_locker);
    tcp_notify_terminal(pcb);
    return tcp_request_release(pcb);
}

net_err_t tcp_close(tcp_pcb_t *pcb)
{
    if (pcb == 0 || tcp_find_slot(pcb) < 0)
        return NET_ERR_PARAM;
    nlocker_lock(&pcb->state_locker);
    int opened = pcb->opened;
    int release_pending = pcb->release_pending;
    tcp_state_t state = pcb->state;
    nlocker_unlock(&pcb->state_locker);
    if (!opened)
        return NET_ERR_PARAM;
    if (release_pending)
        return NET_ERR_OK;
    if (state == TCP_STATE_CLOSED || state == TCP_STATE_TIME_WAIT ||
        state == TCP_STATE_SYN_SENT || state == TCP_STATE_FIN_WAIT_1 ||
        state == TCP_STATE_FIN_WAIT_2)
        return tcp_abort_and_release(pcb);
    if (state != TCP_STATE_ESTABLISHED)
        return NET_ERR_STATE;
    if (pcb->outstanding != 0)
        return NET_ERR_FULL;

    uint32_t end = pcb->snd_nxt + 1U;
    pktbuf_t *fin = tcp_make_segment(pcb, pcb->snd_nxt, pcb->rcv_nxt,
                                     TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0);
    if (fin == 0)
        return NET_ERR_MEM;
    net_err_t err = tcp_start_outstanding(pcb, fin, end);
    if (err < 0)
        return err;
    pcb->snd_nxt = end;
    pcb->state = TCP_STATE_FIN_WAIT_1;
    return NET_ERR_OK;
}

static tcp_pcb_t *tcp_find_pcb(netif_t *netif, const ipaddr_t *src,
                               const ipaddr_t *dest, uint16_t src_port,
                               uint16_t dest_port)
{
    for (int i = 0; i < TCP_PCB_MAX; i++) {
        tcp_pcb_t *pcb = pcbs[i];

        if (pcb != 0 && pcb->opened && pcb->netif == netif &&
            pcb->local_port == dest_port &&
            pcb->remote_port == src_port &&
            ipaddr_is_equal(&pcb->local_ip, dest) &&
            ipaddr_is_equal(&pcb->remote_ip, src))
            return pcb;
    }
    return 0;
}

static net_err_t tcp_accept_ack(tcp_pcb_t *pcb, uint32_t ack)
{
    if ((int32_t)(ack - pcb->snd_una) < 0 ||
        (int32_t)(ack - pcb->snd_nxt) > 0)
        return NET_ERR_STATE;
    if (pcb->outstanding != 0 && ack == pcb->outstanding_end) {
        pcb->snd_una = ack;
        tcp_clear_outstanding(pcb);
        if (pcb->state == TCP_STATE_FIN_WAIT_1)
            pcb->state = TCP_STATE_FIN_WAIT_2;
    }
    return NET_ERR_OK;
}

static net_err_t tcp_queue_data(tcp_pcb_t *pcb, pktbuf_t *buf,
                                int header_size, int payload_size)
{
    if (payload_size > TCP_MSS)
        return NET_ERR_SIZE;
    nlocker_lock(&pcb->recv_locker);
    if (payload_size > TCP_RECV_MAX - pcb->recv_count) {
        nlocker_unlock(&pcb->recv_locker);
        return NET_ERR_FULL;
    }
    int was_empty = pcb->recv_count == 0;
    uint8_t payload[TCP_MSS];
    pktbuf_reset_acc(buf);
    if (pktbuf_seek(buf, header_size) < 0 ||
        pktbuf_read(buf, payload, payload_size) < 0) {
        nlocker_unlock(&pcb->recv_locker);
        return NET_ERR_SIZE;
    }
    int tail = (pcb->recv_head + pcb->recv_count) % TCP_RECV_MAX;
    int first = TCP_RECV_MAX - tail;
    if (first > payload_size)
        first = payload_size;
    plat_memcpy(pcb->recv_storage + tail, payload, (size_t)first);
    if (first < payload_size)
        plat_memcpy(pcb->recv_storage, payload + first,
                    (size_t)(payload_size - first));
    pcb->recv_count += payload_size;
    if (was_empty)
        sys_sem_notify(pcb->recv_done);
    nlocker_unlock(&pcb->recv_locker);
    return NET_ERR_OK;
}

net_err_t tcp_in(netif_t *netif, const ipaddr_t *src,
                 const ipaddr_t *dest, pktbuf_t *buf)
{
    if (netif == 0 || src == 0 || dest == 0 || buf == 0)
        return NET_ERR_PARAM;
    int total = pktbuf_total(buf);
    if (total < TCP_HEADER_SIZE ||
        pktbuf_set_cont(buf, TCP_HEADER_SIZE) < 0)
        return NET_ERR_SIZE;
    tcp_hdr_t *header = (tcp_hdr_t *)pktbuf_data(buf);
    net_err_t err = tcp_header_check(header, total);
    if (err < 0)
        return err;
    int header_size = (header->data_offset >> 4) * 4;
    if ((header->flags & ~(TCP_FLAG_FIN | TCP_FLAG_SYN |
                           TCP_FLAG_PSH | TCP_FLAG_ACK)) != 0)
        return NET_ERR_NOT_SUPPORT;
    if (tcp_checksum(buf, src, dest, (uint16_t)total) != 0)
        return NET_ERR_CHKSUM;

    uint16_t src_port = x_ntohs(header->src_port);
    uint16_t dest_port = x_ntohs(header->dest_port);
    uint32_t seq = x_ntohl(header->seq);
    uint32_t ack = x_ntohl(header->ack);
    uint8_t flags = header->flags;
    uint16_t window = x_ntohs(header->window);
    tcp_pcb_t *pcb = tcp_find_pcb(netif, src, dest, src_port, dest_port);
    if (pcb == 0)
        return NET_ERR_UNREACH;

    if (pcb->state == TCP_STATE_SYN_SENT) {
        if (total != header_size)
            return NET_ERR_FORMAT;
        if (flags != (TCP_FLAG_SYN | TCP_FLAG_ACK) ||
            tcp_state_accept_ack(pcb->state, ack, pcb->iss) < 0)
            return NET_ERR_STATE;
        pcb->window = window;
        pcb->snd_una = ack;
        pcb->rcv_nxt = seq + 1U;
        tcp_clear_outstanding(pcb);
        err = tcp_send_ack(pcb);
        if (err < 0)
            return tcp_fail(pcb, err);
        pcb->state = TCP_STATE_ESTABLISHED;
        nlocker_lock(&pcb->state_locker);
        int connect_waiters = pcb->connect_waiters;
        nlocker_unlock(&pcb->state_locker);
        for (int i = 0; i < (connect_waiters != 0 ? connect_waiters : 1);
             i++)
            sys_sem_notify(pcb->connect_done);
        pktbuf_free(buf);
        return NET_ERR_OK;
    }

    if (pcb->state != TCP_STATE_ESTABLISHED &&
        pcb->state != TCP_STATE_FIN_WAIT_1 &&
        pcb->state != TCP_STATE_FIN_WAIT_2)
        return NET_ERR_STATE;
    if ((flags & TCP_FLAG_ACK) == 0 || (flags & TCP_FLAG_SYN) != 0)
        return NET_ERR_STATE;
    err = tcp_accept_ack(pcb, ack);
    if (err < 0)
        return err;
    pcb->window = window;

    int payload_size = total - header_size;
    int32_t sequence_delta = (int32_t)(seq - pcb->rcv_nxt);
    if (payload_size > 0 && sequence_delta == 0) {
        err = tcp_queue_data(pcb, buf, header_size, payload_size);
        if (err < 0)
            return err;
        pcb->rcv_nxt += (uint32_t)payload_size;
    }
    if (payload_size > 0) {
        err = tcp_send_ack(pcb);
        if (err < 0)
            return err;
    }

    if ((flags & TCP_FLAG_FIN) != 0) {
        uint32_t fin_seq = seq + (uint32_t)payload_size;
        if (fin_seq == pcb->rcv_nxt) {
            pcb->rcv_nxt++;
            err = tcp_send_ack(pcb);
            if (err < 0)
                return err;
            if (pcb->state == TCP_STATE_FIN_WAIT_2) {
                nlocker_lock(&pcb->state_locker);
                pcb->state = TCP_STATE_TIME_WAIT;
                int recv_waiters = pcb->recv_waiters;
                nlocker_unlock(&pcb->state_locker);
                for (int i = 0; i < recv_waiters; i++)
                    sys_sem_notify(pcb->recv_done);
                err = net_timer_add(&pcb->time_wait_timer, "tcp-time-wait",
                                    tcp_time_wait_proc, pcb,
                                    TCP_TIME_WAIT_MS, 0);
                if (err < 0)
                    return tcp_fail(pcb, err);
            }
        } else if (payload_size == 0) {
            err = tcp_send_ack(pcb);
            if (err < 0)
                return err;
        }
    }

    pktbuf_free(buf);
    return NET_ERR_OK;
}

net_err_t tcp_header_check(const tcp_hdr_t *header, int size)
{
    if (header == 0 || size < TCP_HEADER_SIZE)
        return NET_ERR_SIZE;

    int header_size = (header->data_offset >> 4) * 4;
    if (header_size < TCP_HEADER_SIZE || header_size > size)
        return NET_ERR_SIZE;
    if ((header->data_offset & 0x0fU) != 0)
        return NET_ERR_FORMAT;
    return NET_ERR_OK;
}

uint16_t tcp_checksum(pktbuf_t *buf, const ipaddr_t *src,
                      const ipaddr_t *dest, uint16_t length)
{
    if (buf == 0 || src == 0 || dest == 0)
        return 0;

    tcp_pseudo_t pseudo;

    ipaddr_to_buf(src, pseudo.src);
    ipaddr_to_buf(dest, pseudo.dest);
    pseudo.zero = 0;
    pseudo.protocol = NET_PROTOCOL_TCP;
    pseudo.length = x_htons(length);
    uint32_t sum = checksum16(0, &pseudo, sizeof(pseudo), 0, 0);
    pktbuf_reset_acc(buf);
    return pktbuf_checksum16(buf, length, sum, 1);
}

int tcp_sequence_in_window(uint32_t sequence, uint32_t start,
                           uint16_t window)
{
    return window != 0 && (uint32_t)(sequence - start) < window;
}

net_err_t tcp_state_accept_ack(tcp_state_t state, uint32_t ack,
                               uint32_t expected_ack)
{
    if (state != TCP_STATE_SYN_SENT || ack != expected_ack + 1U)
        return NET_ERR_STATE;
    return NET_ERR_OK;
}
