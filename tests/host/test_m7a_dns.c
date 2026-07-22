#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <timeros/net/dns.h>
#include <timeros/net/ipaddr.h>

static int make_response(uint8_t *packet, uint16_t id, uint16_t flags,
                         uint16_t type)
{
    static const uint8_t question[] = {
        3, 'm', '7', 'a', 4, 't', 'e', 's', 't', 0,
        0, 1, 0, 1,
    };
    int length = 0;

    packet[length++] = (uint8_t)(id >> 8);
    packet[length++] = (uint8_t)id;
    packet[length++] = (uint8_t)(flags >> 8);
    packet[length++] = (uint8_t)flags;
    packet[length++] = 0;
    packet[length++] = 1;
    packet[length++] = 0;
    packet[length++] = 1;
    packet[length++] = 0;
    packet[length++] = 0;
    packet[length++] = 0;
    packet[length++] = 0;
    memcpy(packet + length, question, sizeof(question));
    length += sizeof(question);
    packet[length++] = 0xc0;
    packet[length++] = 0x0c;
    packet[length++] = (uint8_t)(type >> 8);
    packet[length++] = (uint8_t)type;
    packet[length++] = 0;
    packet[length++] = 1;
    packet[length++] = 0;
    packet[length++] = 0;
    packet[length++] = 0;
    packet[length++] = 30;
    packet[length++] = 0;
    packet[length++] = 4;
    packet[length++] = 192;
    packet[length++] = 168;
    packet[length++] = 100;
    packet[length++] = 1;
    return length;
}

static void test_query(void)
{
    dns_query_t query;
    assert(dns_query_encode(&query, 0x1234, "m7a.test") == NET_ERR_OK);
    assert(query.length == 26);
    assert(query.bytes[0] == 0x12 && query.bytes[1] == 0x34);
    assert(query.bytes[2] == 1 && query.bytes[3] == 0);
    assert(query.bytes[12] == 3 && query.bytes[16] == 4);
    assert(query.bytes[21] == 0 && query.bytes[22] == 0 &&
           query.bytes[23] == 1);
    assert(query.bytes[24] == 0 && query.bytes[25] == 1);
    assert(dns_query_encode(&query, 1, "") == NET_ERR_PARAM);
    assert(dns_query_encode(&query, 1, "bad..name") == NET_ERR_PARAM);
    assert(dns_query_encode(&query, 1, "bad.") == NET_ERR_PARAM);
}

static void test_response(void)
{
    uint8_t packet[128];
    ipaddr_t address;
    int length = make_response(packet, 0x1234, 0x8180, 1);

    assert(dns_response_parse(packet, length, 0x1234, "m7a.test",
                              &address) == NET_ERR_OK);
    assert(address.q_addr == 0xc0a86401U);
    assert(dns_response_parse(packet, length, 0x4321, "m7a.test",
                              &address) == NET_ERR_PARAM);

    packet[3] = 3;
    assert(dns_response_parse(packet, length, 0x1234, "m7a.test",
                              &address) == NET_ERR_UNREACH);
    packet[3] = 0x80;
    packet[24] = 5;
    assert(dns_response_parse(packet, length, 0x1234, "m7a.test",
                              &address) == NET_ERR_NOT_SUPPORT);
}

static void test_malformed(void)
{
    uint8_t packet[128];
    ipaddr_t address;
    int length = make_response(packet, 0x1234, 0x8180, 1);

    assert(dns_response_parse(packet, 12, 0x1234, "m7a.test",
                              &address) == NET_ERR_SIZE);
    packet[12] = 0xc0;
    packet[13] = 0x0c;
    assert(dns_response_parse(packet, length, 0x1234, "m7a.test",
                              &address) == NET_ERR_FORMAT);
}

int main(void)
{
    test_query();
    test_response();
    test_malformed();
    return 0;
}
