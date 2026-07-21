#include <timeros/net/dns.h>
#include <timeros/net/net_port.h>

#define DNS_HEADER_SIZE 12
#define DNS_FLAG_RESPONSE 0x8000
#define DNS_FLAG_TRUNCATED 0x0200
#define DNS_TYPE_A 1
#define DNS_CLASS_IN 1

static u16 read_u16(const u8 *data)
{
    return ((u16)data[0] << 8) | data[1];
}

static void write_u16(u8 *data, u16 value)
{
    data[0] = (u8)(value >> 8);
    data[1] = (u8)value;
}

static int append_name(u8 *packet, int *length, const char *name)
{
    const char *cursor = name;
    int encoded = 1;

    while (*cursor != '\0') {
        const char *label = cursor;
        int label_length = 0;
        while (*cursor != '\0' && *cursor != '.') {
            cursor++;
            label_length++;
        }
        if (label_length == 0 || label_length > 63 ||
            encoded + label_length + 1 > 255)
            return NET_ERR_PARAM;
        packet[(*length)++] = (u8)label_length;
        plat_memcpy(packet + *length, label, label_length);
        *length += label_length;
        encoded += label_length + 1;
        if (*cursor == '.') {
            cursor++;
            if (*cursor == '\0')
                return NET_ERR_PARAM;
        }
    }
    packet[(*length)++] = 0;
    return NET_ERR_OK;
}

int dns_query_encode(dns_query_t *query, u16 id, const char *name)
{
    if (query == 0 || name == 0 || *name == '\0')
        return NET_ERR_PARAM;

    plat_memset(query, 0, sizeof(*query));
    query->id = id;
    write_u16(query->bytes, id);
    write_u16(query->bytes + 2, 0x0100);
    write_u16(query->bytes + 4, 1);
    int length = DNS_HEADER_SIZE;
    int result = append_name(query->bytes, &length, name);
    if (result < 0)
        return result;
    write_u16(query->bytes + length, DNS_TYPE_A);
    length += 2;
    write_u16(query->bytes + length, DNS_CLASS_IN);
    query->length = length + 2;
    return NET_ERR_OK;
}

static int parse_name(const u8 *packet, int length, int offset,
                      char *name, int name_size, int *next)
{
    int cursor = offset;
    int output = 0;
    int jumped = 0;
    int jumps = 0;

    if (offset < 0 || offset >= length || next == 0)
        return NET_ERR_SIZE;
    while (1) {
        if (cursor >= length)
            return NET_ERR_SIZE;
        u8 label = packet[cursor++];
        if (label == 0) {
            if (name != 0) {
                if (output >= name_size)
                    return NET_ERR_SIZE;
                name[output] = '\0';
            }
            if (!jumped)
                *next = cursor;
            return NET_ERR_OK;
        }
        if ((label & 0xc0) == 0xc0) {
            if (cursor >= length || ++jumps > length)
                return NET_ERR_FORMAT;
            int target = ((label & 0x3f) << 8) | packet[cursor++];
            if (target >= length)
                return NET_ERR_FORMAT;
            if (!jumped) {
                *next = cursor;
                jumped = 1;
            }
            cursor = target;
            continue;
        }
        if ((label & 0xc0) != 0 || label > 63 || cursor + label > length)
            return NET_ERR_FORMAT;
        if (name != 0) {
            if (output != 0) {
                if (output + 1 >= name_size)
                    return NET_ERR_SIZE;
                name[output++] = '.';
            }
            if (output + label >= name_size)
                return NET_ERR_SIZE;
            plat_memcpy(name + output, packet + cursor, label);
            output += label;
        }
        cursor += label;
    }
}

int dns_response_parse(const u8 *packet, int length, u16 id,
                       const char *name, ipaddr_t *address)
{
    if (packet == 0 || name == 0 || address == 0)
        return NET_ERR_PARAM;
    if (length < DNS_HEADER_SIZE)
        return NET_ERR_SIZE;
    if (read_u16(packet) != id)
        return NET_ERR_PARAM;
    u16 flags = read_u16(packet + 2);
    if ((flags & DNS_FLAG_RESPONSE) == 0)
        return NET_ERR_FORMAT;
    if ((flags & DNS_FLAG_TRUNCATED) != 0)
        return NET_ERR_SIZE;
    if ((flags & 0x000f) != 0)
        return NET_ERR_UNREACH;
    if (read_u16(packet + 4) != 1 || read_u16(packet + 6) == 0)
        return NET_ERR_FORMAT;

    char question[256];
    int cursor;
    int result = parse_name(packet, length, DNS_HEADER_SIZE, question,
                            sizeof(question), &cursor);
    if (result < 0)
        return result;
    size_t expected_length = plat_strlen(name);
    if (plat_strlen(question) != expected_length ||
        plat_memcmp(question, name, expected_length) != 0)
        return NET_ERR_PARAM;
    if (cursor + 4 > length || read_u16(packet + cursor) != DNS_TYPE_A ||
        read_u16(packet + cursor + 2) != DNS_CLASS_IN)
        return NET_ERR_NOT_SUPPORT;
    cursor += 4;

    u16 answers = read_u16(packet + 6);
    for (u16 i = 0; i < answers; i++) {
        int next;
        result = parse_name(packet, length, cursor, 0, 0, &next);
        if (result < 0)
            return result;
        cursor = next;
        if (cursor + 10 > length)
            return NET_ERR_SIZE;
        u16 type = read_u16(packet + cursor);
        u16 class = read_u16(packet + cursor + 2);
        u16 data_length = read_u16(packet + cursor + 8);
        cursor += 10;
        if (cursor + data_length > length)
            return NET_ERR_SIZE;
        if (type == DNS_TYPE_A && class == DNS_CLASS_IN &&
            data_length == IPV4_ADDR_SIZE) {
            ipaddr_from_buf(address, packet + cursor);
            return NET_ERR_OK;
        }
        cursor += data_length;
    }
    return NET_ERR_NOT_SUPPORT;
}
