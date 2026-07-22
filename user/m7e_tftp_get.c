#include <timeros/string.h>
#include <timeros/syscall.h>
#include "sha256.h"

int tftp_rrq_encode(unsigned char *, int, const char *);
int tftp_rrq_window_encode(unsigned char *, int, const char *, int);
int tftp_data_parse(const unsigned char *, int, uint16_t,
                    const unsigned char **, int *);
int tftp_ack_encode(unsigned char *, int, uint16_t);
int tftp_oack_window_parse(const unsigned char *, int, int *);

#define FILE_SIZE (1024 * 1024)

static uint16_t net_port(uint16_t value) { return (uint16_t)((value << 8) | (value >> 8)); }

static int fail(int netfd, int filefd, const char *name)
{
    if (filefd >= 0) sys_file_close(filefd);
    if (netfd >= 0) sys_close(netfd);
    printf("QS:TEST_FAIL:m7e-%s\n", name);
    return -1;
}

static int same_digest(const unsigned char *left, const unsigned char *right)
{
    unsigned char difference = 0;
    for (int i = 0; i < 32; i++) difference |= left[i] ^ right[i];
    return difference == 0;
}

int main(void)
{
    static const unsigned char expected[32] = {
        0xfb,0xba,0xb2,0x89,0xf7,0xf9,0x4b,0x25,0x73,0x6c,0x58,0xbe,
        0x46,0xa9,0x94,0xc4,0x41,0xfd,0x02,0x55,0x2c,0xc6,0x02,0x23,
        0x52,0xe3,0xd8,0x6d,0x2f,0xab,0x7c,0x83,
    };
    unsigned char packet[516], rrq[64], ack[4], digest[32];
    const unsigned char *data;
    net_sockaddr_in server = { NET_AF_INET, net_port(69), 0xc0a86401U };
    net_sockaddr_in source;
    uint16_t server_port = 0;
    size_t source_length = sizeof(source);
    sha256_ctx_t hash;
    int netfd = -1, filefd = -1, rrq_length, total = 0;

    rrq_length = tftp_rrq_window_encode(rrq, sizeof(rrq), "m7e.bin", 4);
    netfd = sys_socket(NET_AF_INET, NET_SOCK_DGRAM, 0);
    filefd = sys_file_open("m7e.bin", 1);
    if (rrq_length < 0 || netfd < 0 || filefd < 0 ||
        sys_sendto(netfd, rrq, (size_t)rrq_length, 0, &server, sizeof(server)) != rrq_length)
        return fail(netfd, filefd, "rrq");
    printf("QS:M7E_TFTP_RRQ_OK\n");
    sha256_init(&hash);
    for (int block = 1; block <= 2049; block++) {
        int received;
        int data_length;
        int retries = 0;
        for (;;) {
            received = sys_recvfrom(netfd, packet, sizeof(packet), 0,
                                    &source, &source_length, 3000);
            if (received == -4) {
                if (retries++ == 3)
                    return fail(netfd, filefd, "timeout");
                if (block == 1) {
                    if (sys_sendto(netfd, rrq, (size_t)rrq_length, 0,
                                   &server, sizeof(server)) != rrq_length)
                        return fail(netfd, filefd, "retry-rrq");
                } else {
                    if (tftp_ack_encode(ack, sizeof(ack),
                                        (uint16_t)(block - 1)) < 0 ||
                        sys_sendto(netfd, ack, sizeof(ack), 0, &source,
                                   sizeof(source)) != 4)
                        return fail(netfd, filefd, "retry-ack");
                }
                continue;
            }
            if (received < 0 || source.address != server.address ||
                source.port == 0 ||
                (server_port != 0 && source.port != server_port))
                return fail(netfd, filefd, "data");
            if (received >= 2 && packet[0] == 0 && packet[1] == 6) {
                int window_size;
                if (tftp_oack_window_parse(packet, received, &window_size) < 0 ||
                    window_size != 4)
                    return fail(netfd, filefd, "oack");
                if (server_port == 0)
                    server_port = source.port;
                if (tftp_ack_encode(ack, sizeof(ack), 0) < 0 ||
                    sys_sendto(netfd, ack, sizeof(ack), 0, &source,
                               sizeof(source)) != 4)
                    return fail(netfd, filefd, "oack");
                continue;
            }
            if (received >= 4 && packet[0] == 0 && packet[1] == 3) {
                uint16_t received_block = (uint16_t)(((uint16_t)packet[2] << 8) |
                                                      packet[3]);
                if (received_block == (uint16_t)(block - 1)) {
                    if (tftp_ack_encode(ack, sizeof(ack), received_block) < 0 ||
                        sys_sendto(netfd, ack, sizeof(ack), 0, &source,
                                   sizeof(source)) != 4)
                        return fail(netfd, filefd, "duplicate-ack");
                    continue;
                }
            }
            if (tftp_data_parse(packet, received, (uint16_t)block,
                                &data, &data_length) < 0)
                return fail(netfd, filefd, "data");
            break;
        }
        if (server_port == 0)
            server_port = source.port;
        else if (source.port != server_port)
            return fail(netfd, filefd, "tid");
        if ((block <= 2048 && data_length != 512) || (block == 2049 && data_length != 0))
            return fail(netfd, filefd, "length");
        if (data_length != 0) {
            if (sys_file_write(filefd, data, (size_t)data_length) != data_length)
                return fail(netfd, filefd, "write");
            sha256_update(&hash, data, (unsigned int)data_length);
            total += data_length;
        }
        if (tftp_ack_encode(ack, sizeof(ack), (uint16_t)block) < 0 ||
            sys_sendto(netfd, ack, sizeof(ack), 0, &source, sizeof(source)) != 4)
            return fail(netfd, filefd, "ack");
    }
    if (total != FILE_SIZE) return fail(netfd, filefd, "size");
    printf("QS:M7E_TFTP_1M_OK\n");
    sha256_final(&hash, digest);
    if (!same_digest(digest, expected)) return fail(netfd, filefd, "sha256");
    printf("QS:M7E_TFTP_SHA256_OK\n");
    if (sys_file_sync(filefd) < 0 || sys_file_close(filefd) < 0)
        return fail(netfd, -1, "close-write");
    filefd = sys_file_open("m7e.bin", 0);
    if (filefd < 0) return fail(netfd, -1, "reopen");
    sha256_init(&hash);
    unsigned char readback[512];
    int read_total = 0;
    for (;;) {
        int count = sys_file_read(filefd, readback, sizeof(readback));
        if (count < 0) return fail(netfd, filefd, "readback");
        if (count == 0) break;
        read_total += count;
        sha256_update(&hash, readback, (unsigned int)count);
    }
    sha256_final(&hash, digest);
    if (read_total != FILE_SIZE || !same_digest(digest, expected) ||
        sys_file_close(filefd) < 0)
        return fail(netfd, -1, "readback-hash");
    filefd = -1;
    printf("QS:M7E_TFTP_REOPEN_OK\n");
    server.port = net_port(70);
    if (sys_sendto(netfd, rrq, (size_t)rrq_length, 0, &server, sizeof(server)) != rrq_length ||
        sys_recvfrom(netfd, packet, sizeof(packet), 0, 0, 0, 20) != -4)
        return fail(netfd, -1, "timeout");
    printf("QS:M7E_TFTP_TIMEOUT_OK\n");
    if (sys_close(netfd) < 0 || sys_dns_complete() < 0)
        return fail(-1, -1, "complete");
    while (1) sys_yield();
}
