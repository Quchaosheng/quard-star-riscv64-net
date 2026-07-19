#include <assert.h>

#include <timeros/net/socket.h>
#include <timeros/net/net_sys.h>
#include <timeros/net/pktbuf.h>
#include <timeros/net/ether.h>
#include <timeros/net/ipv4.h>
#include <timeros/net/loop.h>
#include <timeros/net/netif.h>

int main(void)
{
    int first;
    int second;

    assert(net_sys_init() == NET_ERR_OK);
    assert(pktbuf_init() == NET_ERR_OK);
    assert(udp_init() == NET_ERR_OK);
    assert(net_socket_init() == NET_ERR_OK);
    first = net_socket_open(NET_SOCKET_UDP);
    second = net_socket_open(NET_SOCKET_UDP);
    assert(first >= 0);
    assert(second >= 0 && second != first);
    assert(net_socket_bind(first, 4001) == NET_ERR_OK);
    assert(net_socket_bind(second, 4001) == NET_ERR_EXIST);
    assert(net_socket_close(first) == NET_ERR_OK);
    assert(net_socket_bind(second, 4001) == NET_ERR_OK);
    assert(net_socket_close(second) == NET_ERR_OK);
    assert(net_socket_close(first) == NET_ERR_PARAM);
    assert(net_socket_bind(99, 4001) == NET_ERR_PARAM);

    assert(netif_init() == NET_ERR_OK);
    assert(ether_init() == NET_ERR_OK);
    assert(ipv4_init() == NET_ERR_OK);
    assert(loop_init() == NET_ERR_OK);
    assert(udp_init() == NET_ERR_OK);
    assert(net_socket_init() == NET_ERR_OK);
    first = net_socket_open(NET_SOCKET_UDP);
    second = net_socket_open(NET_SOCKET_UDP);
    assert(net_socket_bind(first, 4600) == NET_ERR_OK);
    assert(net_socket_bind(second, 4700) == NET_ERR_OK);
    static const uint8_t payload[] = "socket-udp";
    uint8_t received[16];
    ipaddr_t source;
    uint16_t source_port;
    netif_t *loop = loop_get_netif();
    assert(net_socket_sendto(first, loop, &loop->ipaddr, 4700, payload,
                             sizeof(payload) - 1) == NET_ERR_OK);
    pktbuf_t *packet = netif_get_in(loop, -1);
    assert(packet != 0);
    assert(ipv4_in(loop, packet) == NET_ERR_OK);
    assert(net_socket_recvfrom(second, received, sizeof(received), &source,
                               &source_port, -1) ==
           (int)sizeof(payload) - 1);
    assert(source_port == 4600);
    assert(net_socket_close(first) == NET_ERR_OK);
    assert(net_socket_sendto(first, loop, &loop->ipaddr, 4700, payload,
                             sizeof(payload) - 1) == NET_ERR_PARAM);
    assert(net_socket_close(second) == NET_ERR_OK);
    assert(netif_set_deactive(loop) == NET_ERR_OK);
    assert(netif_close(loop) == NET_ERR_OK);
    return 0;
}
