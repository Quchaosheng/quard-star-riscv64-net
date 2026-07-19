#include <timeros/net/socket.h>

typedef struct _socket_entry_t {
    udp_pcb_t udp;
    uint32_t generation;
    int used;
    int closing;
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

net_err_t net_socket_init(void)
{
    for (int i = 0; i < NET_SOCKET_MAX; i++) {
        entries[i].used = 0;
        entries[i].closing = 0;
        entries[i].generation = 1;
    }
    nlocker_init(&socket_locker, NLOCKER_THREAD);
    return NET_ERR_OK;
}

int net_socket_open(int type)
{
    if (type != NET_SOCKET_UDP)
        return NET_ERR_NOT_SUPPORT;
    for (int i = 0; i < NET_SOCKET_MAX; i++) {
        socket_entry_t *entry = &entries[i];
        if (!entry->used) {
            if (udp_open(&entry->udp) < 0)
                return NET_ERR_MEM;
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

    if (entry == 0)
        return NET_ERR_PARAM;
    return udp_bind(&entry->udp, port);
}

net_err_t net_socket_close(int handle)
{
    int slot;
    uint32_t generation;
    nlocker_lock(&socket_locker);
    socket_entry_t *entry = socket_find(handle);

    if (entry == 0 || socket_decode(handle, &slot, &generation) < 0) {
        nlocker_unlock(&socket_locker);
        return NET_ERR_PARAM;
    }
    entry->closing = 1;
    nlocker_unlock(&socket_locker);
    net_err_t err = udp_close(&entry->udp);
    nlocker_lock(&socket_locker);
    if (err < 0) {
        entry->closing = 0;
        nlocker_unlock(&socket_locker);
        return err;
    }
    entry->used = 0;
    entry->closing = 0;
    entry->generation++;
    if (entry->generation > 0x7fffffU)
        entry->generation = 1;
    nlocker_unlock(&socket_locker);
    return NET_ERR_OK;
}

net_err_t net_socket_sendto(int handle, netif_t *netif,
                            const ipaddr_t *dest, uint16_t dest_port,
                            const uint8_t *data, int size)
{
    socket_entry_t *entry = socket_find(handle);

    if (entry == 0)
        return NET_ERR_PARAM;
    return udp_sendto(&entry->udp, netif, dest, dest_port, data, size);
}

int net_socket_recvfrom(int handle, uint8_t *data, int size, ipaddr_t *src,
                        uint16_t *src_port, int timeout_ms)
{
    if (data == 0 || size < 0)
        return NET_ERR_PARAM;
    nlocker_lock(&socket_locker);
    socket_entry_t *entry = socket_find(handle);

    if (entry == 0 || udp_recv_acquire(&entry->udp) < 0) {
        nlocker_unlock(&socket_locker);
        return NET_ERR_PARAM;
    }
    nlocker_unlock(&socket_locker);
    return udp_recvfrom_acquired(&entry->udp, data, size, src, src_port,
                                 timeout_ms);
}
