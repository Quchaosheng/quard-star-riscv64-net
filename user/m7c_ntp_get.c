#include <timeros/string.h>
#include <timeros/syscall.h>

int ntp_request_encode(unsigned char *packet, int length,
                       uint32_t transmit_seconds);
int ntp_response_parse(const unsigned char *packet, int length,
                       const unsigned char *request, uint64_t *unix_seconds);

static uint16_t net_port(uint16_t value)
{
    return (uint16_t)((value << 8) | (value >> 8));
}

static int fail(int fd, const char *name)
{
    if (fd >= 0)
        sys_close(fd);
    printf("QS:TEST_FAIL:m7c-%s\n", name);
    return -1;
}

int main(void)
{
    unsigned char request[48];
    unsigned char response[96];
    net_sockaddr_in peer = {
        .family = NET_AF_INET,
        .port = net_port(123),
        .address = 0xc0a86401U,
    };
    net_sockaddr_in timeout_peer = peer;
    uint64_t seconds = 0;
    size_t peer_length = sizeof(peer);
    int fd;
    int received;

    if (ntp_request_encode(request, sizeof(request), 100) < 0)
        return fail(-1, "encode");
    fd = sys_socket(NET_AF_INET, NET_SOCK_DGRAM, 0);
    if (fd < 0 || sys_sendto(fd, request, sizeof(request), 0,
                             &peer, sizeof(peer)) != (int)sizeof(request))
        return fail(fd, "send");
    printf("QS:M7C_NTP_QUERY_OK\n");
    received = sys_recvfrom(fd, response, sizeof(response), 0, &peer,
                            &peer_length, 2000);
    if (received < 0 || ntp_response_parse(response, received, request,
                                            &seconds) < 0 || seconds != 123)
        return fail(fd, "response");
    printf("QS:M7C_NTP_RESPONSE_OK\n");

    timeout_peer.port = net_port(124);
    if (sys_sendto(fd, request, sizeof(request), 0, &timeout_peer,
                   sizeof(timeout_peer)) != (int)sizeof(request) ||
        sys_recvfrom(fd, response, sizeof(response), 0, 0, 0, 20) != -4)
        return fail(fd, "timeout");
    printf("QS:M7C_NTP_TIMEOUT_OK\n");
    if (sys_close(fd) < 0)
        return fail(-1, "close");
#ifdef QS_M7D_TEST
    if (sys_exec("m7d_tftp_get") < 0)
        return fail(-1, "tftp-exec");
#else
    if (sys_dns_complete() < 0)
        return fail(-1, "complete");
#endif
    while (1)
        sys_yield();
}
