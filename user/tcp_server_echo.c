#include <timeros/string.h>
#include <timeros/syscall.h>

static uint16_t net_port(uint16_t value)
{
    return (uint16_t)((value << 8) | (value >> 8));
}

static int fail(const char *name)
{
    printf("QS:TEST_FAIL:m6c2-%s\n", name);
    return -1;
}

int main(void)
{
    static const char payload[] = "quard-star-m6c2";
    char received[sizeof(payload) - 1];
    net_sockaddr_in local = {
        .family = NET_AF_INET,
        .port = net_port(4801),
        .address = 0xc0a86402U,
    };
    net_sockaddr_in peer;
    size_t peer_length = sizeof(peer);
    int listener = sys_socket(NET_AF_INET, NET_SOCK_STREAM, 0);

    if (listener < 0 || sys_bind(listener, &local, sizeof(local)) < 0 ||
        sys_listen(listener, 4) < 0)
        return fail("listen");
    printf("QS:M6C2_LISTEN_OK\n");

    int client = sys_accept(listener, &peer, &peer_length);
    if (client < 0)
        return fail("accept");
    printf("QS:M6C2_ACCEPT_OK\n");

    int total = 0;
    while (total < (int)sizeof(received)) {
        int result = sys_recv(client, received + total,
                              sizeof(received) - (size_t)total, 0);
        if (result <= 0)
            return fail("recv");
        total += result;
    }
    if (memcmp(received, payload, sizeof(received)) != 0)
        return fail("echo");
    if (sys_send(client, payload, sizeof(received), 0) !=
        (int)sizeof(received))
        return fail("send");
    if (sys_close(client) < 0)
        return fail("client-close");
    if (sys_close(listener) < 0)
        return fail("listener-close");
    while (1)
        sys_yield();
}
