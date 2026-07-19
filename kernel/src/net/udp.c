#include <timeros/net/udp.h>

#include <timeros/net/net_port.h>
#include <timeros/net/tools.h>

static udp_pcb_t *pcbs[UDP_PCB_MAX];

net_err_t udp_init(void)
{
    plat_memset(pcbs, 0, sizeof(pcbs));
    return NET_ERR_OK;
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
    pcb->open = 0;
    pcb->local_port = 0;
    pcbs[slot] = 0;
    return NET_ERR_OK;
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
