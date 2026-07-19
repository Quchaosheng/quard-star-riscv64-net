#include <assert.h>

#include <timeros/net/socket.h>
#include <timeros/net/net_sys.h>
#include <timeros/net/pktbuf.h>

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
    return 0;
}
