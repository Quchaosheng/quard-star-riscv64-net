#include <timeros/net/socket.h>

typedef struct _socket_entry_t {
    union {
        udp_pcb_t udp;
        tcp_pcb_t tcp;
    } pcb;
    uint32_t generation;
    int used;
    int closing;
    int retired;
    int type;
} socket_entry_t;

static socket_entry_t entries[NET_SOCKET_MAX];
static nlocker_t socket_locker;

static int socket_decode(int handle, int *slot, uint32_t *generation)
{
    int index = handle & 0xff;
    uint32_t value = (unsigned int)handle >> 8;

    if (handle < 0 || index < 0 || index >= NET_SOCKET_MAX || value == 0)
        return -1;
    *slot = index;
    *generation = value;
    return 0;
}

static socket_entry_t *socket_find(int handle)
{
    int slot;
    uint32_t generation;

    if (socket_decode(handle, &slot, &generation) < 0)
        return 0;
    socket_entry_t *entry = &entries[slot];
    return entry->used && !entry->closing && entry->generation == generation ?
           entry : 0;
}

static socket_entry_t *socket_find_used(int handle)
{
    int slot;
    uint32_t generation;

    if (socket_decode(handle, &slot, &generation) < 0)
        return 0;
    socket_entry_t *entry = &entries[slot];
    return entry->used && entry->generation == generation ? entry : 0;
}

static void socket_invalidate(socket_entry_t *entry)
{
    entry->used = 0;
    entry->closing = 0;
    if (entry->generation == 0x7fffffU)
        entry->retired = 1;
    else
        entry->generation++;
}

net_err_t net_socket_init(void)
{
    for (int i = 0; i < NET_SOCKET_MAX; i++) {
        entries[i].used = 0;
        entries[i].closing = 0;
        entries[i].retired = 0;
        entries[i].generation = 1;
    }
    nlocker_init(&socket_locker, NLOCKER_THREAD);
    return NET_ERR_OK;
}

int net_socket_open(int type)
{
    if (type != NET_SOCKET_UDP && type != NET_SOCKET_TCP)
        return NET_ERR_NOT_SUPPORT;
    for (int i = 0; i < NET_SOCKET_MAX; i++) {
        socket_entry_t *entry = &entries[i];
        if (!entry->used && !entry->retired) {
            net_err_t err = type == NET_SOCKET_UDP ?
                            udp_open(&entry->pcb.udp) :
                            tcp_open(&entry->pcb.tcp);
            if (err < 0)
                return err;
            entry->used = 1;
            entry->closing = 0;
            entry->type = type;
            return ((int)entry->generation << 8) | i;
        }
    }
    return NET_ERR_FULL;
}

net_err_t net_socket_bind(int handle, uint16_t port)
{
    socket_entry_t *entry = socket_find(handle);

    if (entry == 0 || entry->type != NET_SOCKET_UDP)
        return NET_ERR_PARAM;
    return udp_bind(&entry->pcb.udp, port);
}

net_err_t net_socket_close(int handle)
{
    nlocker_lock(&socket_locker);
    socket_entry_t *entry = socket_find_used(handle);

    if (entry == 0) {
        nlocker_unlock(&socket_locker);
        return NET_ERR_PARAM;
    }
    if (entry->type == NET_SOCKET_TCP && entry->closing) {
        if (!entry->pcb.tcp.opened) {
            socket_invalidate(entry);
            nlocker_unlock(&socket_locker);
            return NET_ERR_OK;
        }
        nlocker_unlock(&socket_locker);
        net_err_t err = tcp_close(&entry->pcb.tcp);
        nlocker_lock(&socket_locker);
        if (err >= 0 && !entry->pcb.tcp.opened)
            socket_invalidate(entry);
        int pending = err >= 0 && entry->used;
        nlocker_unlock(&socket_locker);
        return err < 0 ? err : pending ? NET_ERR_NONE : NET_ERR_OK;
    }
    if (entry->closing) {
        nlocker_unlock(&socket_locker);
        return NET_ERR_PARAM;
    }
    entry->closing = 1;
    nlocker_unlock(&socket_locker);
    net_err_t err = entry->type == NET_SOCKET_UDP ?
                    udp_close(&entry->pcb.udp) : tcp_close(&entry->pcb.tcp);
    nlocker_lock(&socket_locker);
    if (err < 0) {
        entry->closing = 0;
        nlocker_unlock(&socket_locker);
        return err;
    }
    if (entry->type == NET_SOCKET_TCP && entry->pcb.tcp.opened) {
        nlocker_unlock(&socket_locker);
        return NET_ERR_NONE;
    }
    socket_invalidate(entry);
    nlocker_unlock(&socket_locker);
    return NET_ERR_OK;
}

net_err_t net_socket_connect_start(int handle, netif_t *netif,
                                   const ipaddr_t *dest, uint16_t dest_port)
{
    socket_entry_t *entry = socket_find(handle);

    if (entry == 0 || entry->type != NET_SOCKET_TCP)
        return NET_ERR_PARAM;
    return tcp_connect_start(&entry->pcb.tcp, netif, dest, dest_port);
}

net_err_t net_socket_wait_connect(int handle, int timeout_ms)
{
    nlocker_lock(&socket_locker);
    socket_entry_t *entry = socket_find(handle);
    if (entry == 0 || entry->type != NET_SOCKET_TCP) {
        nlocker_unlock(&socket_locker);
        return NET_ERR_PARAM;
    }
    tcp_pcb_t *pcb = &entry->pcb.tcp;
    nlocker_unlock(&socket_locker);
    return tcp_wait_connect(pcb, timeout_ms);
}

net_err_t net_socket_send(int handle, const uint8_t *data, int size)
{
    socket_entry_t *entry = socket_find(handle);

    if (entry == 0 || entry->type != NET_SOCKET_TCP)
        return NET_ERR_PARAM;
    return tcp_send_start(&entry->pcb.tcp, data, size);
}

int net_socket_recv(int handle, uint8_t *data, int size, int timeout_ms)
{
    nlocker_lock(&socket_locker);
    socket_entry_t *entry = socket_find(handle);
    if (entry == 0 || entry->type != NET_SOCKET_TCP) {
        nlocker_unlock(&socket_locker);
        return NET_ERR_PARAM;
    }
    tcp_pcb_t *pcb = &entry->pcb.tcp;
    nlocker_unlock(&socket_locker);
    return tcp_recv_bytes(pcb, data, size, timeout_ms);
}

net_err_t net_socket_wait_close(int handle, int timeout_ms)
{
    nlocker_lock(&socket_locker);
    socket_entry_t *entry = socket_find_used(handle);
    if (entry == 0 || entry->type != NET_SOCKET_TCP || !entry->closing) {
        nlocker_unlock(&socket_locker);
        return NET_ERR_PARAM;
    }
    if (!entry->pcb.tcp.opened) {
        nlocker_unlock(&socket_locker);
        return NET_ERR_OK;
    }
    tcp_pcb_t *pcb = &entry->pcb.tcp;
    nlocker_lock(&pcb->state_locker);
    int complete = pcb->state == TCP_STATE_CLOSED && pcb->release_pending;
    nlocker_unlock(&pcb->state_locker);
    nlocker_unlock(&socket_locker);
    if (complete)
        return NET_ERR_OK;
    return tcp_wait_close(pcb, timeout_ms);
}

net_err_t net_socket_sendto(int handle, netif_t *netif,
                            const ipaddr_t *dest, uint16_t dest_port,
                            const uint8_t *data, int size)
{
    socket_entry_t *entry = socket_find(handle);

    if (entry == 0 || entry->type != NET_SOCKET_UDP)
        return NET_ERR_PARAM;
    return udp_sendto(&entry->pcb.udp, netif, dest, dest_port, data, size);
}

int net_socket_recvfrom(int handle, uint8_t *data, int size, ipaddr_t *src,
                        uint16_t *src_port, int timeout_ms)
{
    if (data == 0 || size < 0)
        return NET_ERR_PARAM;
    nlocker_lock(&socket_locker);
    socket_entry_t *entry = socket_find(handle);

    if (entry == 0 || entry->type != NET_SOCKET_UDP ||
        udp_recv_acquire(&entry->pcb.udp) < 0) {
        nlocker_unlock(&socket_locker);
        return NET_ERR_PARAM;
    }
    nlocker_unlock(&socket_locker);
    return udp_recvfrom_acquired(&entry->pcb.udp, data, size, src, src_port,
                                 timeout_ms);
}
