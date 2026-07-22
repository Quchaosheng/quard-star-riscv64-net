#include <timeros/string.h>
#include <timeros/syscall.h>

int tftp_rrq_encode(unsigned char *, int, const char *);
int tftp_data_parse(const unsigned char *, int, uint16_t,
                    const unsigned char **, int *);
int tftp_ack_encode(unsigned char *, int, uint16_t);
uint32_t tftp_checksum_update(uint32_t, const unsigned char *, int);

static uint16_t net_port(uint16_t value)
{
    return (uint16_t)((value << 8) | (value >> 8));
}

static int fail(int fd, const char *name)
{
    if (fd >= 0)
        sys_close(fd);
    printf("QS:TEST_FAIL:m7d-%s\n", name);
    return -1;
}

int main(void)
{
#ifdef QS_M7E_TEST
    if (sys_exec("m7e_tftp_get") < 0)
        return fail(-1, "tftp-exec");
#endif
    unsigned char packet[516];
    unsigned char rrq[32];
    const unsigned char *data;
    net_sockaddr_in server = {
        .family = NET_AF_INET,
        .port = net_port(69),
        .address = 0xc0a86401U,
    };
    net_sockaddr_in source;
    size_t source_length = sizeof(source);
    uint32_t checksum = 2166136261U;
    int rrq_length;
    int fd;
    int data_length;
    int total = 0;

    rrq_length = tftp_rrq_encode(rrq, sizeof(rrq), "m7d.bin");
    fd = sys_socket(NET_AF_INET, NET_SOCK_DGRAM, 0);
    if (rrq_length < 0 || fd < 0 ||
        sys_sendto(fd, rrq, (size_t)rrq_length, 0, &server,
                   sizeof(server)) != rrq_length)
        return fail(fd, "rrq");
    printf("QS:M7D_TFTP_RRQ_OK\n");

    int block = 1;
    while (block <= 2) {
        int received = sys_recvfrom(fd, packet, sizeof(packet), 0, &source,
                                    &source_length, 2000);
        if (received < 0 || source.address != server.address ||
            source.port == 0 ||
            tftp_data_parse(packet, received, (uint16_t)block, &data,
                            &data_length) < 0)
            return fail(fd, "data");
        if ((block == 1 && data_length != 512) ||
            (block == 2 && data_length != 188))
            return fail(fd, "length");
        checksum = tftp_checksum_update(checksum, data, data_length);
        total += data_length;
        unsigned char ack[4];
        if (tftp_ack_encode(ack, sizeof(ack), (uint16_t)block) < 0 ||
            sys_sendto(fd, ack, sizeof(ack), 0, &source, sizeof(source)) != 4)
            return fail(fd, "ack");
        printf("QS:M7D_TFTP_DATA%d_OK\n", block);
        block++;
    }
    if (total != 700 || checksum != 0xffde9949U)
        return fail(fd, "checksum");
    printf("QS:M7D_TFTP_CHECKSUM_OK\n");

    server.port = net_port(70);
    if (sys_sendto(fd, rrq, (size_t)rrq_length, 0, &server,
                   sizeof(server)) != rrq_length ||
        sys_recvfrom(fd, packet, sizeof(packet), 0, 0, 0, 20) != -4)
        return fail(fd, "timeout");
    printf("QS:M7D_TFTP_TIMEOUT_OK\n");
    if (sys_close(fd) < 0)
        return fail(-1, "close");
#ifndef QS_M7E_TEST
    if (sys_dns_complete() < 0)
        return fail(-1, "complete");
#endif
    while (1)
        sys_yield();
}
