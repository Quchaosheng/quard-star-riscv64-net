#include <timeros/net/ipaddr.h>

static int parse_octet(const char **cursor, uint8_t *value)
{
    const char *p = *cursor;
    const char *start = p;
    unsigned int number = 0;
    int digits = 0;

    while (*p >= '0' && *p <= '9') {
        number = number * 10U + (unsigned int)(*p - '0');
        if (number > 255U)
            return -1;
        p++;
        digits++;
    }
    if (digits == 0 || (digits > 1 && start[0] == '0'))
        return -1;
    *value = (uint8_t)number;
    *cursor = p;
    return 0;
}

void ipaddr_set_any(ipaddr_t *ip)
{
    if (ip != 0)
        ip->q_addr = 0;
}

net_err_t ipaddr_from_str(ipaddr_t *dest, const char *str)
{
    if (dest == 0 || str == 0)
        return NET_ERR_PARAM;

    uint8_t bytes[IPV4_ADDR_SIZE];
    const char *cursor = str;
    for (int i = 0; i < IPV4_ADDR_SIZE; i++) {
        if (parse_octet(&cursor, &bytes[i]) < 0)
            return NET_ERR_PARAM;
        if (i != IPV4_ADDR_SIZE - 1) {
            if (*cursor != '.')
                return NET_ERR_PARAM;
            cursor++;
        }
    }
    if (*cursor != '\0')
        return NET_ERR_PARAM;

    ipaddr_from_buf(dest, bytes);
    return NET_ERR_OK;
}

ipaddr_t *ipaddr_get_any(void)
{
    static ipaddr_t any;
    return &any;
}

void ipaddr_copy(ipaddr_t *dest, const ipaddr_t *src)
{
    if (dest != 0 && src != 0)
        dest->q_addr = src->q_addr;
}

int ipaddr_is_equal(const ipaddr_t *left, const ipaddr_t *right)
{
    return left != 0 && right != 0 && left->q_addr == right->q_addr;
}

void ipaddr_to_buf(const ipaddr_t *src, uint8_t *ip_buf)
{
    if (src == 0 || ip_buf == 0)
        return;
    ip_buf[0] = (uint8_t)(src->q_addr >> 24);
    ip_buf[1] = (uint8_t)(src->q_addr >> 16);
    ip_buf[2] = (uint8_t)(src->q_addr >> 8);
    ip_buf[3] = (uint8_t)src->q_addr;
}

void ipaddr_from_buf(ipaddr_t *dest, const uint8_t *ip_buf)
{
    if (dest == 0 || ip_buf == 0)
        return;
    dest->q_addr = ((uint32_t)ip_buf[0] << 24) |
                   ((uint32_t)ip_buf[1] << 16) |
                   ((uint32_t)ip_buf[2] << 8) | ip_buf[3];
}

int ipaddr_is_local_broadcast(const ipaddr_t *ip)
{
    return ip != 0 && ip->q_addr == IPV4_ADDR_BROADCAST;
}

int ipaddr_is_direct_broadcast(const ipaddr_t *ip,
                               const ipaddr_t *netmask)
{
    return ip != 0 && netmask != 0 &&
           (ip->q_addr & ~netmask->q_addr) ==
               (IPV4_ADDR_BROADCAST & ~netmask->q_addr);
}

int ipaddr_is_any(const ipaddr_t *ip)
{
    return ip != 0 && ip->q_addr == 0;
}

ipaddr_t ipaddr_get_net(const ipaddr_t *ip, const ipaddr_t *netmask)
{
    ipaddr_t network = { 0 };
    if (ip != 0 && netmask != 0)
        network.q_addr = ip->q_addr & netmask->q_addr;
    return network;
}

int ipaddr_is_match(const ipaddr_t *dest, const ipaddr_t *src,
                    const ipaddr_t *netmask)
{
    if (dest == 0 || src == 0 || netmask == 0)
        return 0;
    ipaddr_t dest_net = ipaddr_get_net(dest, netmask);
    ipaddr_t src_net = ipaddr_get_net(src, netmask);
    if (ipaddr_is_local_broadcast(dest) ||
        (ipaddr_is_direct_broadcast(dest, netmask) &&
         ipaddr_is_equal(&dest_net, &src_net)))
        return 1;
    return ipaddr_is_equal(dest, src);
}

void ipaddr_set_all_1(ipaddr_t *ip)
{
    if (ip != 0)
        ip->q_addr = IPV4_ADDR_BROADCAST;
}

int ipaddr_1_cnt(const ipaddr_t *ip)
{
    if (ip == 0)
        return 0;
    uint32_t value = ip->q_addr;
    int count = 0;
    while (value != 0) {
        count += (int)(value & 1U);
        value >>= 1;
    }
    return count;
}

void ipaddr_set_loop(ipaddr_t *ip)
{
    if (ip != 0)
        ip->q_addr = 0x7f000001U;
}
