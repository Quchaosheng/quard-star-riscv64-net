#include <assert.h>

#include <timeros/net/ipv4.h>
#include <timeros/net/net_sys.h>
#include <timeros/net/protocol.h>
#include <timeros/net/tcp.h>
#include <timeros/net/timer.h>

net_err_t ipv4_register_handler(uint8_t protocol,
                                ipv4_input_handler_t handler)
{
    assert(protocol == NET_PROTOCOL_TCP);
    assert(handler == tcp_in);
    return NET_ERR_OK;
}

net_err_t ipv4_out(netif_t *netif, const ipaddr_t *dest, uint8_t protocol,
                   pktbuf_t *buf)
{
    (void)netif;
    (void)dest;
    assert(protocol == NET_PROTOCOL_TCP);
    assert(buf != 0);
    pktbuf_free(buf);
    return NET_ERR_OK;
}

int main(void)
{
    tcp_pcb_t *pcbs[TCP_PCB_MAX + 1] = { 0 };

    assert(net_sys_init() == NET_ERR_OK);
    assert(pktbuf_init() == NET_ERR_OK);
    assert(net_timer_init() == NET_ERR_OK);
    assert(tcp_init() == NET_ERR_OK);

    for (int i = 0; i < TCP_PCB_MAX; i++) {
        assert(tcp_open(&pcbs[i]) == NET_ERR_OK);
        assert(pcbs[i] != 0);
        for (int j = 0; j < i; j++)
            assert(pcbs[i] != pcbs[j]);
    }
    assert(tcp_open(&pcbs[TCP_PCB_MAX]) == NET_ERR_MEM);
    assert(pcbs[TCP_PCB_MAX] == 0);

    tcp_pcb_t *released = pcbs[3];
    assert(tcp_close(pcbs[3]) == NET_ERR_OK);
    pcbs[3] = 0;
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    assert(tcp_open(&pcbs[3]) == NET_ERR_OK);
    assert(pcbs[3] == released);

    for (int i = 0; i < TCP_PCB_MAX; i++)
        assert(tcp_close(pcbs[i]) == NET_ERR_OK);
    assert(net_timer_check_tmo(1) == NET_ERR_OK);
    return 0;
}
