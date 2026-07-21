#include <stddef.h>
#include <stdint.h>
#include <timeros/net/net_err.h>

#define NTP_PACKET_SIZE 48
#define NTP_UNIX_EPOCH 2208988800ULL

static uint32_t ntp_read32(const unsigned char *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

static void ntp_write32(unsigned char *data, uint32_t value)
{
    data[0] = (unsigned char)(value >> 24);
    data[1] = (unsigned char)(value >> 16);
    data[2] = (unsigned char)(value >> 8);
    data[3] = (unsigned char)value;
}

int ntp_request_encode(unsigned char *packet, int length,
                       uint32_t transmit_seconds)
{
    if (packet == 0 || length < NTP_PACKET_SIZE)
        return NET_ERR_PARAM;
    for (int index = 0; index < NTP_PACKET_SIZE; index++)
        packet[index] = 0;
    packet[0] = 0x1b;
    ntp_write32(packet + 40, transmit_seconds + (uint32_t)NTP_UNIX_EPOCH);
    return NTP_PACKET_SIZE;
}

int ntp_response_parse(const unsigned char *packet, int length,
                       const unsigned char *request, uint64_t *unix_seconds)
{
    uint32_t transmit;
    unsigned char version;

    if (packet == 0 || request == 0 || unix_seconds == 0)
        return NET_ERR_PARAM;
    if (length < NTP_PACKET_SIZE)
        return NET_ERR_SIZE;
    version = (unsigned char)((packet[0] >> 3) & 7);
    if ((packet[0] & 7) != 4 || version < 3 || version > 4)
        return NET_ERR_FORMAT;
    if (packet[1] == 0 || packet[1] > 15)
        return NET_ERR_FORMAT;
    for (int index = 0; index < 8; index++) {
        if (packet[24 + index] != request[40 + index])
            return NET_ERR_FORMAT;
    }
    transmit = ntp_read32(packet + 40);
    if ((uint64_t)transmit < NTP_UNIX_EPOCH)
        return NET_ERR_FORMAT;
    *unix_seconds = (uint64_t)transmit - NTP_UNIX_EPOCH;
    return NET_ERR_OK;
}
