#include <timeros/string.h>
#include <timeros/syscall.h>

#define STRESS_PARALLEL 8
#define STRESS_RECONNECTS 100

static uint16_t net_port(uint16_t value)
{
    return (uint16_t)((value << 8) | (value >> 8));
}

static int fail(const char *name)
{
    printf("QS:TEST_FAIL:m6c2-%s\n", name);
    return -1;
}

#ifdef QS_M6C2_STRESS
static void stress_payload(char payload[9], int index)
{
    memcpy(payload, "m6c2-000", 9);
    payload[5] = (char)('0' + index / 100);
    payload[6] = (char)('0' + (index / 10) % 10);
    payload[7] = (char)('0' + index % 10);
}

static int echo_client(int client, int index)
{
    char expected[9];
    char received[8];
    int total = 0;

    stress_payload(expected, index);
    while (total < (int)sizeof(received)) {
        int result = sys_recv(client, received + total,
                              sizeof(received) - (size_t)total, 0);
        if (result <= 0)
            return -1;
        total += result;
    }
    if (memcmp(received, expected, sizeof(received)) != 0)
        return -1;
    return sys_send(client, expected, sizeof(received), 0) ==
           (int)sizeof(received) ? 0 : -1;
}

static int wait_peer_close(int client)
{
    char byte;
    return sys_recv(client, &byte, 1, 0) < 0 ? 0 : -1;
}

static int accept_stress_client(int listener, int index)
{
    net_sockaddr_in peer;
    size_t peer_length = sizeof(peer);
    int client = sys_accept(listener, &peer, &peer_length);

    if (client < 0 || echo_client(client, index) < 0)
        return -1;
    return client;
}

static int run_stress_server(int listener)
{
    int clients[STRESS_PARALLEL];

    for (int i = 0; i < STRESS_PARALLEL; i++) {
        clients[i] = accept_stress_client(listener, i);
        if (clients[i] < 0)
            return -1;
        if (i == 0)
            printf("QS:M6C2_ACCEPT_OK\n");
    }
    for (int i = 0; i < STRESS_PARALLEL; i++) {
        if (wait_peer_close(clients[i]) < 0 ||
            sys_close(clients[i]) < 0)
            return -1;
    }
    for (int i = 0; i < STRESS_RECONNECTS; i++) {
        int client = accept_stress_client(listener, STRESS_PARALLEL + i);
        if (client < 0 || wait_peer_close(client) < 0 ||
            sys_close(client) < 0)
            return -1;
    }
    return 0;
}
#endif

int main(void)
{
#ifndef QS_M6C2_STRESS
    static const char payload[] = "quard-star-m6c2";
    char received[sizeof(payload) - 1];
#endif
    net_sockaddr_in local = {
        .family = NET_AF_INET,
        .port = net_port(4801),
        .address = 0xc0a86402U,
    };
#ifndef QS_M6C2_STRESS
    net_sockaddr_in peer;
    size_t peer_length = sizeof(peer);
#endif
    int listener = sys_socket(NET_AF_INET, NET_SOCK_STREAM, 0);

    if (listener < 0 || sys_bind(listener, &local, sizeof(local)) < 0 ||
        sys_listen(listener, 4) < 0)
        return fail("listen");
    printf("QS:M6C2_LISTEN_OK\n");

#ifdef QS_M6C2_STRESS
    if (run_stress_server(listener) < 0)
        return fail("stress");
#else
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
#endif
    if (sys_close(listener) < 0)
        return fail("listener-close");
    while (1)
        sys_yield();
}
