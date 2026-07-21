#include <timeros/string.h>
#include <timeros/syscall.h>

int http_response_validate(const char *data, int length,
                           const char *expected_body, int expected_length);

static uint16_t net_port(uint16_t value)
{
    return (uint16_t)((value << 8) | (value >> 8));
}

static int fail(int fd, const char *name)
{
    if (fd >= 0)
        sys_close(fd);
    printf("QS:TEST_FAIL:m7b-%s\n", name);
    return -1;
}

int main(void)
{
    static const char request[] =
        "GET /m7b.txt HTTP/1.0\r\n"
        "Host: m7a.test\r\n"
        "Connection: close\r\n\r\n";
    static const char body[] = "m7b-http-body";
    char response[512];
    uint32_t address = 0;
    net_sockaddr_in peer;
    int fd;
    int received = 0;

    if (sys_dns_resolve("m7a.test", &address) < 0 ||
        address != 0xc0a86401U)
        return fail(-1, "dns");
    printf("QS:M7B_HTTP_DNS_OK\n");

    peer.family = NET_AF_INET;
    peer.port = net_port(4800);
    peer.address = address;
    fd = sys_socket(NET_AF_INET, NET_SOCK_STREAM, 0);
    if (fd < 0 || sys_connect(fd, &peer, sizeof(peer)) < 0)
        return fail(fd, "connect");
    printf("QS:M7B_HTTP_CONNECT_OK\n");
    if (sys_send(fd, request, sizeof(request) - 1, 0) !=
        (int)sizeof(request) - 1)
        return fail(fd, "send");

    while (received < (int)sizeof(response)) {
        int result = sys_recv(fd, response + received,
                              sizeof(response) - (size_t)received, 0);
        if (result <= 0)
            return fail(fd, "recv");
        received += result;
        result = http_response_validate(response, received, body,
                                        (int)sizeof(body) - 1);
        if (result == 0)
            break;
        if (result != -6)
            return fail(fd, "response");
    }
    if (received == (int)sizeof(response))
        return fail(fd, "response-size");
    printf("QS:M7B_HTTP_RESPONSE_OK\n");
    int close_result = sys_close(fd);
    if (close_result < 0)
        return fail(-1, "close");
    printf("QS:M7B_HTTP_CLOSE_OK\n");
#ifdef QS_M7C_TEST
    if (sys_exec("m7c_ntp_get") < 0)
        return fail(-1, "ntp-exec");
#else
    if (sys_dns_complete() < 0)
        return fail(-1, "complete");
#endif
    while (1)
        sys_yield();
}
