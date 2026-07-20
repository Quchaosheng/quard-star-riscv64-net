#include <assert.h>
#include <pthread.h>
#include <sched.h>

#include <timeros/net/socket.h>
#include <timeros/net/net_sys.h>
#include <timeros/net/pktbuf.h>
#include <timeros/net/ether.h>
#include <timeros/net/ipv4.h>
#include <timeros/net/loop.h>
#include <timeros/net/netif.h>

static int recv_started;
static int recv_acquired;
static int recv_result;

void udp_test_recv_acquired_hook(void)
{
    __atomic_store_n(&recv_acquired, 1, __ATOMIC_RELEASE);
}

void udp_test_close_marked_hook(void)
{
}

static void *socket_recv_thread(void *arg)
{
    int handle = *(int *)arg;
    uint8_t byte;

    __atomic_store_n(&recv_started, 1, __ATOMIC_RELEASE);
    recv_result = net_socket_recvfrom(handle, &byte, 1, 0, 0, 0);
    return 0;
}

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
    assert(net_socket_bind(first, 0, 0, 4001) == NET_ERR_OK);
    assert(net_socket_bind(second, 0, 0, 4001) == NET_ERR_EXIST);
    assert(net_socket_close(first) == NET_ERR_OK);
    assert(net_socket_bind(second, 0, 0, 4001) == NET_ERR_OK);
    assert(net_socket_close(second) == NET_ERR_OK);
    assert(net_socket_close(first) == NET_ERR_PARAM);
    assert(net_socket_bind(99, 0, 0, 4001) == NET_ERR_PARAM);

    assert(netif_init() == NET_ERR_OK);
    assert(ether_init() == NET_ERR_OK);
    assert(ipv4_init() == NET_ERR_OK);
    assert(loop_init() == NET_ERR_OK);
    assert(udp_init() == NET_ERR_OK);
    assert(net_socket_init() == NET_ERR_OK);
    first = net_socket_open(NET_SOCKET_UDP);
    second = net_socket_open(NET_SOCKET_UDP);
    assert(net_socket_bind(first, 0, 0, 4600) == NET_ERR_OK);
    assert(net_socket_bind(second, 0, 0, 4700) == NET_ERR_OK);
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
    int handles[NET_SOCKET_MAX];
    for (int i = 0; i < NET_SOCKET_MAX; i++) {
        handles[i] = net_socket_open(NET_SOCKET_UDP);
        assert(handles[i] >= 0);
    }
    assert(net_socket_open(NET_SOCKET_UDP) == NET_ERR_FULL);
    for (int i = 0; i < NET_SOCKET_MAX; i++)
        assert(net_socket_close(handles[i]) == NET_ERR_OK);

    int waiting = net_socket_open(NET_SOCKET_UDP);
    assert(waiting >= 0);
    assert(net_socket_bind(waiting, 0, 0, 4800) == NET_ERR_OK);
    pthread_t receiver;
    recv_started = 0;
    recv_acquired = 0;
    assert(pthread_create(&receiver, 0, socket_recv_thread, &waiting) == 0);
    while (!__atomic_load_n(&recv_started, __ATOMIC_ACQUIRE))
        sched_yield();
    while (!__atomic_load_n(&recv_acquired, __ATOMIC_ACQUIRE))
        sched_yield();
    assert(net_socket_close(waiting) == NET_ERR_OK);
    assert(pthread_join(receiver, 0) == 0);
    assert(recv_result == NET_ERR_STATE);
    int reused = net_socket_open(NET_SOCKET_UDP);
    assert(reused >= 0 && reused != waiting);
    assert(net_socket_close(reused) == NET_ERR_OK);
    assert(netif_set_deactive(loop) == NET_ERR_OK);
    assert(netif_close(loop) == NET_ERR_OK);
    return 0;
}
