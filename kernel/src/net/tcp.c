#include <timeros/net/tcp.h>

#include <timeros/net/ipv4.h>
#include <timeros/net/net_port.h>
#include <timeros/net/protocol.h>
#include <timeros/net/tools.h>

#ifdef QS_M6C1_TEST
#include <timeros/selftest.h>
#endif

typedef struct __attribute__((packed)) _tcp_pseudo_t {
    uint8_t src[IPV4_ADDR_SIZE];
    uint8_t dest[IPV4_ADDR_SIZE];
    uint8_t zero;
    uint8_t protocol;
    uint16_t length;
} tcp_pseudo_t;

static tcp_pcb_t pcb_storage[TCP_PCB_MAX];
static tcp_pcb_t *pcbs[TCP_PCB_MAX];
static net_timer_t release_timers[TCP_PCB_MAX];
static nlocker_t table_locker;
static uint16_t next_ephemeral = 49152;
static uint32_t next_iss = 1000;

static net_err_t tcp_release_now_locked(tcp_pcb_t *pcb);
static net_err_t tcp_request_release(tcp_pcb_t *pcb);

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
    nlocker_lock(&pcb->state_locker);
    uint32_t seq = pcb->snd_nxt;
    uint32_t ack_number = pcb->rcv_nxt;
    nlocker_unlock(&pcb->state_locker);
    pktbuf_t *ack = tcp_make_segment(pcb, seq, ack_number,
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

    nlocker_lock(&table_locker);
    nlocker_lock(&pcb->state_locker);
    int expires = pcb->opened && pcb->state == TCP_STATE_TIME_WAIT &&
                  !pcb->release_pending;
    if (expires) {
        pcb->state = TCP_STATE_CLOSED;
        pcb->error = NET_ERR_OK;
    }
    nlocker_unlock(&pcb->state_locker);
    if (!expires) {
        nlocker_unlock(&table_locker);
        return;
    }
    {
        nlocker_lock(&pcb->state_locker);
        int close_waiters = pcb->close_waiters;
        nlocker_unlock(&pcb->state_locker);
        for (int i = 0; i < (close_waiters != 0 ? close_waiters : 1); i++)
            sys_sem_notify(pcb->close_done);
        (void)tcp_request_release(pcb);
    }
    nlocker_unlock(&table_locker);
}

static void tcp_release_proc(net_timer_t *timer, void *arg)
{
    tcp_pcb_t *pcb = (tcp_pcb_t *)arg;

    nlocker_lock(&table_locker);
    if (tcp_find_slot(pcb) < 0) {
        timer->flags = 0;
        nlocker_unlock(&table_locker);
        return;
    }
    nlocker_lock(&pcb->state_locker);
    int release = pcb->opened && pcb->release_pending &&
                  !pcb->socket_attached &&
                  pcb->connect_waiters == 0 && pcb->recv_waiters == 0 &&
                  pcb->close_waiters == 0;
    nlocker_unlock(&pcb->state_locker);
    if (release) {
        timer->flags = 0;
        (void)tcp_release_now_locked(pcb);
    }
    nlocker_unlock(&table_locker);
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
    pcb->peer_fin_seen = 0;
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
    nlocker_init(&table_locker, NLOCKER_THREAD);
    plat_memset(pcb_storage, 0, sizeof(pcb_storage));
    plat_memset(pcbs, 0, sizeof(pcbs));
    plat_memset(release_timers, 0, sizeof(release_timers));
    next_ephemeral = 49152;
    next_iss = 1000;
    return ipv4_register_handler(NET_PROTOCOL_TCP, tcp_in);
}

static net_err_t tcp_alloc_locked(tcp_pcb_t **result, int socket_attached)
{
    if (result == 0)
        return NET_ERR_PARAM;
    *result = 0;
    int slot = -1;
    for (int i = 0; i < TCP_PCB_MAX; i++) {
        if (pcbs[i] == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return NET_ERR_MEM;
    }

    tcp_pcb_t *pcb = &pcb_storage[slot];
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
    pcb->socket_attached = socket_attached;
    pcbs[slot] = pcb;
    *result = pcb;
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

net_err_t tcp_open(tcp_pcb_t **result)
{
    if (result == 0)
        return NET_ERR_PARAM;
    nlocker_lock(&table_locker);
    net_err_t err = tcp_alloc_locked(result, 0);
    nlocker_unlock(&table_locker);
    return err;
}

net_err_t tcp_socket_open(tcp_pcb_t **result)
{
    if (result == 0)
        return NET_ERR_PARAM;
    nlocker_lock(&table_locker);
    net_err_t err = tcp_alloc_locked(result, 1);
    nlocker_unlock(&table_locker);
    return err;
}

net_err_t tcp_connect_start(tcp_pcb_t *pcb, netif_t *netif,
                            const ipaddr_t *remote, uint16_t remote_port)
{
    if (pcb == 0 || netif == 0 || remote == 0 || remote_port == 0)
        return NET_ERR_PARAM;
    nlocker_lock(&table_locker);
    if (tcp_find_slot(pcb) < 0) {
        nlocker_unlock(&table_locker);
        return NET_ERR_STATE;
    }
    nlocker_lock(&pcb->state_locker);
    int unavailable = !pcb->opened || pcb->release_pending ||
                      pcb->connect_waiters != 0 ||
                      pcb->recv_waiters != 0 || pcb->close_waiters != 0 ||
                      pcb->state != TCP_STATE_CLOSED ||
                      pcb->outstanding != 0;
    nlocker_unlock(&pcb->state_locker);
    if (unavailable) {
        nlocker_unlock(&table_locker);
        return NET_ERR_STATE;
    }
    if (netif->state != NETIF_ACTIVE) {
        nlocker_unlock(&table_locker);
        return NET_ERR_STATE;
    }
    uint16_t local_port = tcp_choose_port();
    if (local_port == 0) {
        nlocker_unlock(&table_locker);
        return NET_ERR_FULL;
    }

    tcp_drain_sem(pcb->connect_done);
    tcp_drain_sem(pcb->recv_done);
    tcp_drain_sem(pcb->close_done);
    nlocker_lock(&pcb->recv_locker);
    pcb->recv_head = 0;
    pcb->recv_count = 0;
    nlocker_unlock(&pcb->recv_locker);
    nlocker_lock(&pcb->state_locker);
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
    nlocker_unlock(&pcb->state_locker);
    pktbuf_t *syn = tcp_make_segment(pcb, pcb->iss, 0,
                                     TCP_FLAG_SYN, 0, 0);
    if (syn == 0) {
        nlocker_lock(&pcb->state_locker);
        tcp_reset_connection(pcb);
        nlocker_unlock(&pcb->state_locker);
        nlocker_unlock(&table_locker);
        return NET_ERR_MEM;
    }
    net_err_t err = tcp_start_outstanding(pcb, syn, pcb->snd_nxt);
    if (err < 0) {
        nlocker_lock(&pcb->state_locker);
        tcp_reset_connection(pcb);
        nlocker_unlock(&pcb->state_locker);
        nlocker_unlock(&table_locker);
        return err;
    }
    nlocker_lock(&pcb->state_locker);
    pcb->state = TCP_STATE_SYN_SENT;
    nlocker_unlock(&pcb->state_locker);
    nlocker_unlock(&table_locker);
    return NET_ERR_OK;
}

net_err_t tcp_send_start(tcp_pcb_t *pcb, const uint8_t *data, int size)
{
    if (pcb == 0 || data == 0 || size <= 0 || size > TCP_MSS)
        return NET_ERR_PARAM;
    nlocker_lock(&table_locker);
    if (tcp_find_slot(pcb) < 0) {
        nlocker_unlock(&table_locker);
        return NET_ERR_STATE;
    }
    nlocker_lock(&pcb->state_locker);
    int established = pcb->opened &&
                      pcb->state == TCP_STATE_ESTABLISHED;
    uint16_t window = pcb->window;
    uint32_t seq = pcb->snd_nxt;
    uint32_t ack = pcb->rcv_nxt;
    nlocker_unlock(&pcb->state_locker);
    if (!established) {
        nlocker_unlock(&table_locker);
        return NET_ERR_STATE;
    }
    if (pcb->outstanding != 0) {
        nlocker_unlock(&table_locker);
        return NET_ERR_FULL;
    }
    if (window == 0 || window < (uint16_t)size) {
        nlocker_unlock(&table_locker);
        return NET_ERR_FULL;
    }

    uint32_t end = seq + (uint32_t)size;
    pktbuf_t *segment = tcp_make_segment(pcb, seq, ack,
                                         TCP_FLAG_PSH | TCP_FLAG_ACK,
                                         data, size);
    if (segment == 0) {
        nlocker_unlock(&table_locker);
        return NET_ERR_MEM;
    }
    net_err_t err = tcp_start_outstanding(pcb, segment, end);
    if (err < 0) {
        nlocker_unlock(&table_locker);
        return err;
    }
    nlocker_lock(&pcb->state_locker);
    pcb->snd_nxt = end;
    nlocker_unlock(&pcb->state_locker);
    nlocker_unlock(&table_locker);
    return NET_ERR_OK;
}

static net_err_t tcp_wait_completion_acquire(tcp_pcb_t *pcb, int connect_wait)
{
    if (pcb == 0)
        return NET_ERR_PARAM;
    nlocker_lock(&table_locker);
    if (tcp_find_slot(pcb) < 0) {
        nlocker_unlock(&table_locker);
        return NET_ERR_PARAM;
    }
    nlocker_lock(&pcb->state_locker);
    int *waiters = connect_wait ? &pcb->connect_waiters :
                                  &pcb->close_waiters;
    if (!pcb->opened ||
        (pcb->release_pending &&
         (connect_wait || pcb->state != TCP_STATE_CLOSED))) {
        nlocker_unlock(&pcb->state_locker);
        nlocker_unlock(&table_locker);
        return NET_ERR_STATE;
    }
    if (!connect_wait && pcb->state == TCP_STATE_CLOSED &&
        pcb->release_pending) {
        (*waiters)++;
        nlocker_unlock(&pcb->state_locker);
        nlocker_unlock(&table_locker);
        return NET_ERR_OK;
    }
    if (pcb->error < 0) {
        net_err_t terminal_error = pcb->error;
        nlocker_unlock(&pcb->state_locker);
        nlocker_unlock(&table_locker);
        return terminal_error;
    }
    if (connect_wait) {
        if (pcb->state != TCP_STATE_SYN_SENT &&
            pcb->state != TCP_STATE_ESTABLISHED) {
            nlocker_unlock(&pcb->state_locker);
            nlocker_unlock(&table_locker);
            return NET_ERR_STATE;
        }
    } else if (pcb->state != TCP_STATE_FIN_WAIT_1 &&
               pcb->state != TCP_STATE_FIN_WAIT_2 &&
               pcb->state != TCP_STATE_TIME_WAIT &&
               !(pcb->state == TCP_STATE_CLOSED &&
                 pcb->release_pending)) {
        nlocker_unlock(&pcb->state_locker);
        nlocker_unlock(&table_locker);
        return NET_ERR_STATE;
    }
    (*waiters)++;
    nlocker_unlock(&pcb->state_locker);
    nlocker_unlock(&table_locker);
    return NET_ERR_OK;
}

static net_err_t tcp_wait_completion_acquired(tcp_pcb_t *pcb, int timeout_ms,
                                              int connect_wait)
{
    if (pcb == 0)
        return NET_ERR_PARAM;

    sys_sem_t sem;
    int *waiters;
    net_err_t immediate = NET_ERR_NONE;
    nlocker_lock(&pcb->state_locker);
    sem = connect_wait ? pcb->connect_done : pcb->close_done;
    waiters = connect_wait ? &pcb->connect_waiters : &pcb->close_waiters;
    if (!pcb->opened)
        immediate = NET_ERR_STATE;
    else if (!connect_wait && pcb->state == TCP_STATE_CLOSED &&
             pcb->release_pending)
        immediate = NET_ERR_OK;
    else if (pcb->error < 0)
        immediate = pcb->error;
    else if (connect_wait && pcb->state == TCP_STATE_ESTABLISHED)
        immediate = NET_ERR_OK;
    nlocker_unlock(&pcb->state_locker);

    if (immediate != NET_ERR_NONE) {
        nlocker_lock(&pcb->state_locker);
        (*waiters)--;
        nlocker_unlock(&pcb->state_locker);
        return immediate;
    }

    net_err_t err = sys_sem_wait(sem, timeout_ms);
    nlocker_lock(&pcb->state_locker);
    if (err == NET_ERR_OK && pcb->error < 0)
        err = pcb->error;
    (*waiters)--;
    nlocker_unlock(&pcb->state_locker);
    if (timeout_ms < 0 && err == NET_ERR_TMO)
        return NET_ERR_NONE;
    return err;
}

net_err_t tcp_wait_connect(tcp_pcb_t *pcb, int timeout_ms)
{
    net_err_t err = tcp_wait_connect_acquire(pcb);

    if (err < 0)
        return err;
    return tcp_wait_connect_acquired(pcb, timeout_ms);
}

net_err_t tcp_wait_connect_acquire(tcp_pcb_t *pcb)
{
    return tcp_wait_completion_acquire(pcb, 1);
}

net_err_t tcp_wait_connect_acquired(tcp_pcb_t *pcb, int timeout_ms)
{
    return tcp_wait_completion_acquired(pcb, timeout_ms, 1);
}

net_err_t tcp_wait_close(tcp_pcb_t *pcb, int timeout_ms)
{
    net_err_t err = tcp_wait_close_acquire(pcb);

    if (err < 0)
        return err;
    return tcp_wait_close_acquired(pcb, timeout_ms);
}

net_err_t tcp_wait_close_acquire(tcp_pcb_t *pcb)
{
    return tcp_wait_completion_acquire(pcb, 0);
}

net_err_t tcp_wait_close_acquired(tcp_pcb_t *pcb, int timeout_ms)
{
    return tcp_wait_completion_acquired(pcb, timeout_ms, 0);
}

net_err_t tcp_recv_acquire(tcp_pcb_t *pcb)
{
    if (pcb == 0)
        return NET_ERR_PARAM;
    nlocker_lock(&table_locker);
    if (tcp_find_slot(pcb) < 0) {
        nlocker_unlock(&table_locker);
        return NET_ERR_PARAM;
    }
    nlocker_lock(&pcb->state_locker);
    if (!pcb->opened || pcb->release_pending) {
        nlocker_unlock(&pcb->state_locker);
        nlocker_unlock(&table_locker);
        return NET_ERR_STATE;
    }
    if (pcb->state == TCP_STATE_CLOSED ||
        pcb->state == TCP_STATE_TIME_WAIT) {
        net_err_t state_error = pcb->error < 0 ? pcb->error : NET_ERR_STATE;
        nlocker_unlock(&pcb->state_locker);
        nlocker_unlock(&table_locker);
        return state_error;
    }
    pcb->recv_waiters++;
    nlocker_unlock(&pcb->state_locker);
    nlocker_unlock(&table_locker);
    return NET_ERR_OK;
}

int tcp_recv_bytes_acquired(tcp_pcb_t *pcb, uint8_t *data, int size,
                            int timeout_ms)
{
    if (pcb == 0 || data == 0 || size <= 0)
        return NET_ERR_PARAM;

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
    nlocker_unlock(&pcb->state_locker);
    return result;
}

int tcp_recv_bytes(tcp_pcb_t *pcb, uint8_t *data, int size,
                   int timeout_ms)
{
    if (pcb == 0 || data == 0 || size <= 0)
        return NET_ERR_PARAM;
    net_err_t err = tcp_recv_acquire(pcb);

    if (err < 0)
        return err;
    return tcp_recv_bytes_acquired(pcb, data, size, timeout_ms);
}

net_err_t tcp_retransmit_due(tcp_pcb_t *pcb)
{
    if (pcb == 0)
        return NET_ERR_STATE;
    nlocker_lock(&table_locker);
    if (tcp_find_slot(pcb) < 0) {
        nlocker_unlock(&table_locker);
        return NET_ERR_STATE;
    }
    nlocker_lock(&pcb->state_locker);
    int ready = pcb->opened && pcb->outstanding != 0;
    nlocker_unlock(&pcb->state_locker);
    if (!ready) {
        nlocker_unlock(&table_locker);
        return NET_ERR_STATE;
    }
    net_timer_remove(&pcb->retrans_timer);
    net_err_t err = tcp_output_owned(pcb);
    if (err < 0)
        goto fail;
    pcb->retry_count++;
    if (pcb->retry_count >= TCP_RETRY_MAX) {
        err = tcp_fail(pcb, NET_ERR_TMO);
        nlocker_unlock(&table_locker);
        return err;
    }
    err = tcp_arm_timer(pcb);
    if (err < 0)
        goto fail;
    nlocker_unlock(&table_locker);
    return NET_ERR_OK;

fail:
    err = tcp_fail(pcb, err);
    nlocker_unlock(&table_locker);
    return err;
}

static net_err_t tcp_release_now_locked(tcp_pcb_t *pcb)
{
    if (pcb == 0)
        return NET_ERR_PARAM;
    int slot = tcp_find_slot(pcb);
    if (slot < 0)
        return NET_ERR_PARAM;
    nlocker_lock(&pcb->state_locker);
    int releasable = pcb->release_pending &&
                     !pcb->socket_attached &&
                     pcb->connect_waiters == 0 && pcb->recv_waiters == 0 &&
                     pcb->close_waiters == 0;
    if (!releasable) {
        nlocker_unlock(&pcb->state_locker);
        return NET_ERR_STATE;
    }
    pcb->opened = 0;
    nlocker_unlock(&pcb->state_locker);
    tcp_clear_outstanding(pcb);
    net_timer_remove(&pcb->time_wait_timer);
    net_timer_remove(&release_timers[slot]);
    nlocker_lock(&pcb->recv_locker);
    pcb->recv_head = 0;
    pcb->recv_count = 0;
    nlocker_unlock(&pcb->recv_locker);
    nlocker_lock(&pcb->state_locker);
    tcp_reset_connection(pcb);
    pcb->release_pending = 0;
    pcb->connect_waiters = 0;
    pcb->recv_waiters = 0;
    pcb->close_waiters = 0;
    nlocker_unlock(&pcb->state_locker);
    sys_sem_free(pcb->close_done);
    sys_sem_free(pcb->recv_done);
    sys_sem_free(pcb->connect_done);
    pcb->close_done = SYS_SEM_INVALID;
    pcb->recv_done = SYS_SEM_INVALID;
    pcb->connect_done = SYS_SEM_INVALID;
    nlocker_destroy(&pcb->recv_locker);
    nlocker_destroy(&pcb->state_locker);
#ifdef QS_M6C1_TEST
    m6c1_mark_tcp_close();
#endif
    pcbs[slot] = 0;
    return NET_ERR_OK;
}

static net_err_t tcp_request_release(tcp_pcb_t *pcb)
{
    int slot = tcp_find_slot(pcb);
    if (slot < 0)
        return NET_ERR_PARAM;
    nlocker_lock(&pcb->state_locker);
    pcb->release_pending = 1;
    nlocker_unlock(&pcb->state_locker);
    net_err_t err = net_timer_add(&release_timers[slot], "tcp-release",
                                  tcp_release_proc, pcb, 1,
                                  NET_TIMER_RELOAD);
    return err == NET_ERR_EXIST ? NET_ERR_OK : err;
}

net_err_t tcp_socket_detach(tcp_pcb_t *pcb)
{
    if (pcb == 0)
        return NET_ERR_PARAM;
    nlocker_lock(&table_locker);
    if (tcp_find_slot(pcb) < 0) {
        nlocker_unlock(&table_locker);
        return NET_ERR_PARAM;
    }
    nlocker_lock(&pcb->state_locker);
    int releasable = pcb->opened && pcb->socket_attached &&
                     pcb->release_pending &&
                     pcb->connect_waiters == 0 && pcb->recv_waiters == 0 &&
                     pcb->close_waiters == 0;
    if (!releasable) {
        nlocker_unlock(&pcb->state_locker);
        nlocker_unlock(&table_locker);
        return NET_ERR_STATE;
    }
    pcb->socket_attached = 0;
    nlocker_unlock(&pcb->state_locker);
    net_err_t err = tcp_release_now_locked(pcb);
    nlocker_unlock(&table_locker);
    return err;
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
    if (pcb == 0)
        return NET_ERR_PARAM;
    nlocker_lock(&table_locker);
    if (tcp_find_slot(pcb) < 0) {
        nlocker_unlock(&table_locker);
        return NET_ERR_PARAM;
    }
    nlocker_lock(&pcb->state_locker);
    int opened = pcb->opened;
    int release_pending = pcb->release_pending;
    tcp_state_t state = pcb->state;
    uint32_t snd_nxt = pcb->snd_nxt;
    uint32_t rcv_nxt = pcb->rcv_nxt;
    nlocker_unlock(&pcb->state_locker);
    if (!opened) {
        nlocker_unlock(&table_locker);
        return NET_ERR_PARAM;
    }
    if (release_pending) {
        nlocker_lock(&pcb->state_locker);
        int releasable = pcb->connect_waiters == 0 &&
                         pcb->recv_waiters == 0 && pcb->close_waiters == 0 &&
                         !pcb->socket_attached;
        nlocker_unlock(&pcb->state_locker);
        if (releasable) {
            net_err_t release_err = tcp_release_now_locked(pcb);
            nlocker_unlock(&table_locker);
            return release_err;
        }
        nlocker_unlock(&table_locker);
        return NET_ERR_OK;
    }
    if (state == TCP_STATE_TIME_WAIT)
    {
        nlocker_unlock(&table_locker);
        return NET_ERR_OK;
    }
    if (state == TCP_STATE_CLOSED || state == TCP_STATE_SYN_SENT ||
        state == TCP_STATE_FIN_WAIT_1 ||
        state == TCP_STATE_FIN_WAIT_2)
    {
        net_err_t abort_err = tcp_abort_and_release(pcb);
        nlocker_unlock(&table_locker);
        return abort_err;
    }
    if (state != TCP_STATE_ESTABLISHED) {
        nlocker_unlock(&table_locker);
        return NET_ERR_STATE;
    }
    if (pcb->outstanding != 0) {
        nlocker_unlock(&table_locker);
        return NET_ERR_FULL;
    }

    uint32_t end = snd_nxt + 1U;
    pktbuf_t *fin = tcp_make_segment(pcb, snd_nxt, rcv_nxt,
                                     TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0);
    if (fin == 0) {
        nlocker_unlock(&table_locker);
        return NET_ERR_MEM;
    }
    net_err_t err = tcp_start_outstanding(pcb, fin, end);
    if (err < 0) {
        nlocker_unlock(&table_locker);
        return err;
    }
    nlocker_lock(&pcb->state_locker);
    pcb->snd_nxt = end;
    pcb->state = TCP_STATE_FIN_WAIT_1;
    nlocker_unlock(&pcb->state_locker);
    nlocker_unlock(&table_locker);
    return NET_ERR_OK;
}

static tcp_pcb_t *tcp_find_pcb(netif_t *netif, const ipaddr_t *src,
                               const ipaddr_t *dest, uint16_t src_port,
                               uint16_t dest_port)
{
    for (int i = 0; i < TCP_PCB_MAX; i++) {
        tcp_pcb_t *pcb = pcbs[i];

        if (pcb == 0)
            continue;
        nlocker_lock(&pcb->state_locker);
        int match = pcb->opened && pcb->netif == netif &&
                    pcb->local_port == dest_port &&
                    pcb->remote_port == src_port &&
                    ipaddr_is_equal(&pcb->local_ip, dest) &&
                    ipaddr_is_equal(&pcb->remote_ip, src);
        nlocker_unlock(&pcb->state_locker);
        if (match)
            return pcb;
    }
    return 0;
}

static net_err_t tcp_accept_ack(tcp_pcb_t *pcb, uint32_t ack)
{
#ifdef QS_M6C1_TEST
    int retransmission_ack = 0;
#endif
    nlocker_lock(&pcb->state_locker);
    if ((int32_t)(ack - pcb->snd_una) < 0 ||
        (int32_t)(ack - pcb->snd_nxt) > 0) {
        nlocker_unlock(&pcb->state_locker);
        return NET_ERR_STATE;
    }
    int complete = pcb->outstanding != 0 && ack == pcb->outstanding_end;
    if (complete) {
#ifdef QS_M6C1_TEST
        retransmission_ack = pcb->retry_count != 0 &&
                            pcb->state == TCP_STATE_ESTABLISHED;
#endif
        pcb->snd_una = ack;
        if (pcb->state == TCP_STATE_FIN_WAIT_1) {
            pcb->state = pcb->peer_fin_seen ? TCP_STATE_TIME_WAIT :
                         TCP_STATE_FIN_WAIT_2;
            if (pcb->state == TCP_STATE_TIME_WAIT)
                pcb->peer_fin_seen = 0;
        }
    }
    nlocker_unlock(&pcb->state_locker);
    if (complete)
        tcp_clear_outstanding(pcb);
#ifdef QS_M6C1_TEST
    if (retransmission_ack)
        m6c1_mark_tcp_retrans();
#endif
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

static net_err_t tcp_start_local_fin(tcp_pcb_t *pcb)
{
    nlocker_lock(&pcb->state_locker);
    if (!pcb->opened || pcb->outstanding != 0 ||
        pcb->state != TCP_STATE_ESTABLISHED) {
        nlocker_unlock(&pcb->state_locker);
        return NET_ERR_STATE;
    }
    uint32_t seq = pcb->snd_nxt;
    uint32_t ack = pcb->rcv_nxt;
    nlocker_unlock(&pcb->state_locker);

    pktbuf_t *fin = tcp_make_segment(pcb, seq, ack,
                                     TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0);
    if (fin == 0)
        return NET_ERR_MEM;
    net_err_t err = tcp_start_outstanding(pcb, fin, seq + 1U);
    if (err < 0)
        return err;
    nlocker_lock(&pcb->state_locker);
    pcb->snd_nxt = seq + 1U;
    pcb->state = TCP_STATE_FIN_WAIT_1;
    nlocker_unlock(&pcb->state_locker);
    return NET_ERR_OK;
}

static net_err_t tcp_arm_time_wait(tcp_pcb_t *pcb)
{
    nlocker_lock(&pcb->state_locker);
    if (pcb->state != TCP_STATE_TIME_WAIT) {
        nlocker_unlock(&pcb->state_locker);
        return NET_ERR_STATE;
    }
    pcb->peer_fin_seen = 0;
    int recv_waiters = pcb->recv_waiters;
    nlocker_unlock(&pcb->state_locker);
    for (int i = 0; i < recv_waiters; i++)
        sys_sem_notify(pcb->recv_done);
    net_timer_remove(&pcb->time_wait_timer);
    return net_timer_add(&pcb->time_wait_timer, "tcp-time-wait",
                         tcp_time_wait_proc, pcb, TCP_TIME_WAIT_MS, 0);
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
    nlocker_lock(&table_locker);
    tcp_pcb_t *pcb = tcp_find_pcb(netif, src, dest, src_port, dest_port);
    if (pcb == 0) {
        nlocker_unlock(&table_locker);
        return NET_ERR_UNREACH;
    }
#define TCP_IN_RETURN(value) do { \
        nlocker_unlock(&table_locker); \
        return (value); \
    } while (0)
    int payload_size = total - header_size;
    nlocker_lock(&pcb->state_locker);
    tcp_state_t state = pcb->state;
    uint32_t rcv_nxt = pcb->rcv_nxt;
    uint32_t iss = pcb->iss;
    nlocker_unlock(&pcb->state_locker);

    if (state == TCP_STATE_TIME_WAIT) {
        uint32_t fin_seq = seq + (uint32_t)payload_size;
        if ((flags & TCP_FLAG_FIN) == 0 || (flags & TCP_FLAG_ACK) == 0 ||
            (flags & TCP_FLAG_SYN) != 0 ||
            (fin_seq != rcv_nxt && fin_seq + 1U != rcv_nxt))
            TCP_IN_RETURN(NET_ERR_STATE);
        err = tcp_accept_ack(pcb, ack);
        if (err < 0)
            TCP_IN_RETURN(err);
        err = tcp_send_ack(pcb);
        if (err < 0)
            TCP_IN_RETURN(err);
        net_timer_remove(&pcb->time_wait_timer);
        err = net_timer_add(&pcb->time_wait_timer, "tcp-time-wait",
                            tcp_time_wait_proc, pcb, TCP_TIME_WAIT_MS, 0);
        if (err < 0)
            TCP_IN_RETURN(tcp_fail(pcb, err));
        pktbuf_free(buf);
        TCP_IN_RETURN(NET_ERR_OK);
    }

    if (state == TCP_STATE_SYN_SENT) {
        if (total != header_size)
            TCP_IN_RETURN(NET_ERR_FORMAT);
        if (flags != (TCP_FLAG_SYN | TCP_FLAG_ACK) ||
            tcp_state_accept_ack(state, ack, iss) < 0)
            TCP_IN_RETURN(NET_ERR_STATE);
        nlocker_lock(&pcb->state_locker);
        pcb->window = window;
        pcb->snd_una = ack;
        pcb->rcv_nxt = seq + 1U;
        nlocker_unlock(&pcb->state_locker);
        tcp_clear_outstanding(pcb);
        err = tcp_send_ack(pcb);
        if (err < 0)
            TCP_IN_RETURN(tcp_fail(pcb, err));
        nlocker_lock(&pcb->state_locker);
        pcb->state = TCP_STATE_ESTABLISHED;
        nlocker_unlock(&pcb->state_locker);
#ifdef QS_M6C1_TEST
        m6c1_mark_tcp();
#endif
        nlocker_lock(&pcb->state_locker);
        int connect_waiters = pcb->connect_waiters;
        nlocker_unlock(&pcb->state_locker);
        for (int i = 0; i < (connect_waiters != 0 ? connect_waiters : 1);
             i++)
            sys_sem_notify(pcb->connect_done);
        pktbuf_free(buf);
        TCP_IN_RETURN(NET_ERR_OK);
    }

    if (state != TCP_STATE_ESTABLISHED &&
        state != TCP_STATE_FIN_WAIT_1 &&
        state != TCP_STATE_FIN_WAIT_2)
        TCP_IN_RETURN(NET_ERR_STATE);
    if ((flags & TCP_FLAG_ACK) == 0 || (flags & TCP_FLAG_SYN) != 0)
        TCP_IN_RETURN(NET_ERR_STATE);
    err = tcp_accept_ack(pcb, ack);
    if (err < 0)
        TCP_IN_RETURN(err);
    nlocker_lock(&pcb->state_locker);
    int ack_completed_close = pcb->state == TCP_STATE_TIME_WAIT;
    nlocker_unlock(&pcb->state_locker);
    if (ack_completed_close) {
        err = tcp_arm_time_wait(pcb);
        if (err < 0)
            TCP_IN_RETURN(tcp_fail(pcb, err));
        pktbuf_free(buf);
        TCP_IN_RETURN(NET_ERR_OK);
    }
    nlocker_lock(&pcb->state_locker);
    pcb->window = window;
    rcv_nxt = pcb->rcv_nxt;
    nlocker_unlock(&pcb->state_locker);

    int32_t sequence_delta = (int32_t)(seq - rcv_nxt);
    if (payload_size > 0 && sequence_delta == 0) {
        err = tcp_queue_data(pcb, buf, header_size, payload_size);
        if (err < 0)
            TCP_IN_RETURN(err);
        nlocker_lock(&pcb->state_locker);
        pcb->rcv_nxt += (uint32_t)payload_size;
        nlocker_unlock(&pcb->state_locker);
    }
    if (payload_size > 0) {
        err = tcp_send_ack(pcb);
        if (err < 0)
            TCP_IN_RETURN(err);
    }

    if ((flags & TCP_FLAG_FIN) != 0) {
        uint32_t fin_seq = seq + (uint32_t)payload_size;
        nlocker_lock(&pcb->state_locker);
        int exact_fin = fin_seq == pcb->rcv_nxt;
        tcp_state_t fin_state = pcb->state;
        if (exact_fin)
            pcb->rcv_nxt++;
        nlocker_unlock(&pcb->state_locker);
        if (exact_fin) {
            err = tcp_send_ack(pcb);
            if (err < 0)
                TCP_IN_RETURN(err);
            if (fin_state == TCP_STATE_FIN_WAIT_2) {
                nlocker_lock(&pcb->state_locker);
                pcb->state = TCP_STATE_TIME_WAIT;
                pcb->peer_fin_seen = 0;
                nlocker_unlock(&pcb->state_locker);
                err = tcp_arm_time_wait(pcb);
                if (err < 0)
                    TCP_IN_RETURN(tcp_fail(pcb, err));
            } else if (fin_state == TCP_STATE_ESTABLISHED) {
                nlocker_lock(&pcb->state_locker);
                pcb->peer_fin_seen = 1;
                nlocker_unlock(&pcb->state_locker);
                err = tcp_start_local_fin(pcb);
                if (err < 0)
                    TCP_IN_RETURN(tcp_fail(pcb, err));
            } else if (fin_state == TCP_STATE_FIN_WAIT_1) {
                nlocker_lock(&pcb->state_locker);
                pcb->peer_fin_seen = 1;
                nlocker_unlock(&pcb->state_locker);
            }
        } else if (payload_size == 0) {
            err = tcp_send_ack(pcb);
            if (err < 0)
                TCP_IN_RETURN(err);
        }
    }

    pktbuf_free(buf);
    TCP_IN_RETURN(NET_ERR_OK);
#undef TCP_IN_RETURN
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
