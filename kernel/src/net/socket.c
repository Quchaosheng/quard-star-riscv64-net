#include <timeros/net/socket.h>

typedef struct _socket_entry_t {
    udp_pcb_t udp;
    uint16_t generation;
    int used;
    int type;
} socket_entry_t;

static socket_entry_t entries[NET_SOCKET_MAX];

static int socket_decode(int handle, int *slot, uint16_t *generation)
{
    int index = handle & 0xff;
    uint16_t value = (uint16_t)((unsigned int)handle >> 8);

    if (handle < 0 || index < 0 || index >= NET_SOCKET_MAX || value == 0)
        return -1;
    *slot = index;
    *generation = value;
    return 0;
}

static socket_entry_t *socket_find(int handle)
{
    int slot;
    uint16_t generation;

    if (socket_decode(handle, &slot, &generation) < 0)
        return 0;
    socket_entry_t *entry = &entries[slot];
    return entry->used && entry->generation == generation ? entry : 0;
}

net_err_t net_socket_init(void)
{
    for (int i = 0; i < NET_SOCKET_MAX; i++) {
        entries[i].used = 0;
        entries[i].generation = 1;
    }
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
    uint16_t generation;
    socket_entry_t *entry = socket_find(handle);

    if (entry == 0 || socket_decode(handle, &slot, &generation) < 0)
        return NET_ERR_PARAM;
    net_err_t err = udp_close(&entry->udp);
    entry->used = 0;
    entry->generation++;
    if (entry->generation == 0)
        entry->generation = 1;
    return err;
}
