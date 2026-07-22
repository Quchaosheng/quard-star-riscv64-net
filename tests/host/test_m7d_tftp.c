#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <timeros/net/net_err.h>

int tftp_rrq_encode(unsigned char *, int, const char *);
int tftp_rrq_window_encode(unsigned char *, int, const char *, int);
int tftp_data_parse(const unsigned char *, int, uint16_t,
                    const unsigned char **, int *);
int tftp_ack_encode(unsigned char *, int, uint16_t);
uint32_t tftp_checksum_update(uint32_t, const unsigned char *, int);

int main(void)
{
    unsigned char packet[520];
    const unsigned char *data = 0;
    int data_length = 0;

    assert(tftp_rrq_encode(packet, sizeof(packet), "m7d.bin") == 16);
    assert(memcmp(packet, "\0\1m7d.bin\0octet\0", 16) == 0);
    assert(tftp_rrq_window_encode(packet, sizeof(packet), "m7e.bin", 4) == 29);
    assert(memcmp(packet + 16, "windowsize\0" "4\0", 13) == 0);
    assert(tftp_rrq_encode(packet, sizeof(packet), "../bad") ==
           NET_ERR_PARAM);
    packet[0] = 0;
    packet[1] = 3;
    packet[2] = 0;
    packet[3] = 1;
    memset(packet + 4, 0x5a, 512);
    assert(tftp_data_parse(packet, 516, 1, &data, &data_length) ==
           NET_ERR_OK);
    assert(data == packet + 4 && data_length == 512);
    assert(tftp_data_parse(packet, 516, 2, &data, &data_length) ==
           NET_ERR_STATE);
    packet[1] = 5;
    assert(tftp_data_parse(packet, 516, 1, &data, &data_length) ==
           NET_ERR_FORMAT);
    assert(tftp_ack_encode(packet, sizeof(packet), 2) == 4);
    assert(memcmp(packet, "\0\4\0\2", 4) == 0);
    assert(tftp_checksum_update(2166136261U,
                                (const unsigned char *)"abc", 3) ==
           0x1a47e90bU);
    return 0;
}
