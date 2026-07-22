#include <timeros/string.h>
#include <timeros/syscall.h>

static uint16_t net_port(uint16_t value)
{
    return (uint16_t)((value << 8) | (value >> 8));
}

static int fail(const char *name)
{
    printf("QS:TEST_FAIL:m6c1-%s\n", name);
    return -1;
}

int main(void)
{
    static const char payload[] = "quard-star-m6c1";
    char reply[sizeof(payload)];
    net_sockaddr_in peer = {
        .family = NET_AF_INET,
        .port = net_port(4800),
        .address = 0xc0a86401U,
    };
    int fd = sys_socket(NET_AF_INET, NET_SOCK_STREAM, 0);
    if (fd < 0)
        return fail("socket");
    if (sys_connect(fd, &peer, sizeof(peer)) < 0)
        return fail("connect");
    printf("QS:M6C1_TCP_OK\n");

    if (sys_send(fd, payload, sizeof(payload) - 1, 0) !=
        (int)sizeof(payload) - 1)
        return fail("send");
    int received = 0;
    while (received < (int)sizeof(payload) - 1) {
        int result = sys_recv(fd, reply + received,
                              sizeof(payload) - 1 - (size_t)received, 0);
        if (result <= 0)
            return fail("recv");
        received += result;
    }
    if (memcmp(reply, payload, sizeof(payload) - 1) != 0)
        return fail("echo");
    printf("QS:M6C1_TCP_RETRANS_OK\n");

    if (sys_close(fd) < 0)
        return fail("close");
#ifdef QS_M6C2_TEST
    if (sys_exec("tcp_server_echo") < 0)
        return fail("server-exec");
#endif
    while (1)
        sys_yield();
}
