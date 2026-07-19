#include <timeros/string.h>
#include <timeros/syscall.h>

#define TEST_SOCKET_COUNT 16

static uint16_t net_port(uint16_t value)
{
    return (uint16_t)((value << 8) | (value >> 8));
}

int main(void)
{
    static const char payload[] = "quard-star-m6b";
    char reply[sizeof(payload)];
    net_sockaddr_in local = {
        .family = NET_AF_INET,
        .port = net_port(4600),
        .address = 0,
    };
    net_sockaddr_in peer = {
        .family = NET_AF_INET,
        .port = net_port(4700),
        .address = 0xc0a86401U,
    };
    int sockets[TEST_SOCKET_COUNT];
    for (int i = 0; i < TEST_SOCKET_COUNT; i++) {
        sockets[i] = sys_socket(NET_AF_INET, NET_SOCK_DGRAM, 0);
        if (sockets[i] < 0) {
            printf("QS:TEST_FAIL:m6b-socket-capacity\n");
            return -1;
        }
    }
    int fd = sockets[0];
    if (sys_bind(fd, &local, sizeof(local)) < 0 ||
        sys_sendto(fd, payload, sizeof(payload) - 1, 0,
                   &peer, sizeof(peer)) != (int)sizeof(payload) - 1) {
        printf("QS:TEST_FAIL:m6b-udp-send\n");
        return -1;
    }
    size_t peer_length = sizeof(peer);
    int received = sys_recvfrom(fd, reply, sizeof(reply), 0, &peer,
                                &peer_length, 2000);
    if (received != (int)sizeof(payload) - 1 ||
        memcmp(reply, payload, sizeof(payload) - 1) != 0) {
        printf("QS:TEST_FAIL:m6b-udp-recv\n");
        return -1;
    }
    printf("\nQS:M6B_UDP_OK\n");
    received = sys_recvfrom(fd, reply, sizeof(reply), 0, 0, 0, 20);
    if (received != -4) {
        printf("QS:TEST_FAIL:m6b-udp-timeout\n");
        return -1;
    }
    printf("QS:M6B_UDP_TIMEOUT_OK\n");
    for (int i = 0; i < TEST_SOCKET_COUNT; i++) {
        if (sys_close(sockets[i]) < 0)
            return -1;
    }
    while (1)
        sys_yield();
}
