#include <stddef.h>
#include <stdint.h>
#include <timeros/net/net_err.h>

#define TFTP_DATA_MAX 512

int tftp_rrq_encode(unsigned char *packet, int capacity, const char *filename)
{
    static const char mode[] = "octet";
    int name_length = 0;
    int offset;

    if (packet == 0 || filename == 0)
        return NET_ERR_PARAM;
    while (filename[name_length] != 0) {
        if (filename[name_length] == '/' || filename[name_length] == '\\')
            return NET_ERR_PARAM;
        name_length++;
        if (name_length > 127)
            return NET_ERR_SIZE;
    }
    if (name_length == 0 || capacity < name_length + 9)
        return NET_ERR_SIZE;
    packet[0] = 0;
    packet[1] = 1;
    offset = 2;
    for (int index = 0; index < name_length; index++)
        packet[offset++] = (unsigned char)filename[index];
    packet[offset++] = 0;
    for (int index = 0; index < (int)sizeof(mode) - 1; index++)
        packet[offset++] = (unsigned char)mode[index];
    packet[offset++] = 0;
    return offset;
}

int tftp_data_parse(const unsigned char *packet, int length,
                    uint16_t expected_block, const unsigned char **data,
                    int *data_length)
{
    uint16_t block;

    if (packet == 0 || data == 0 || data_length == 0)
        return NET_ERR_PARAM;
    if (length < 4 || length > TFTP_DATA_MAX + 4)
        return NET_ERR_SIZE;
    if (packet[0] != 0 || packet[1] != 3)
        return NET_ERR_FORMAT;
    block = (uint16_t)(((uint16_t)packet[2] << 8) | packet[3]);
    if (block != expected_block)
        return NET_ERR_STATE;
    *data = packet + 4;
    *data_length = length - 4;
    return NET_ERR_OK;
}

int tftp_ack_encode(unsigned char *packet, int capacity, uint16_t block)
{
    if (packet == 0 || capacity < 4 || block == 0)
        return NET_ERR_PARAM;
    packet[0] = 0;
    packet[1] = 4;
    packet[2] = (unsigned char)(block >> 8);
    packet[3] = (unsigned char)block;
    return 4;
}

uint32_t tftp_checksum_update(uint32_t checksum, const unsigned char *data,
                              int length)
{
    if (data == 0 || length < 0)
        return 0;
    for (int index = 0; index < length; index++) {
        checksum ^= data[index];
        checksum *= 16777619U;
    }
    return checksum;
}
