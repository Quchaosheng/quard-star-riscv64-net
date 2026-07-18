#ifndef TOS_NET_TOOLS_H__
#define TOS_NET_TOOLS_H__

#include <timeros/net/net_port.h>
#include <timeros/net/net_err.h>

static inline u16 net_checksum16(const u8 *data, u32 length, u32 sum)
{
    for (u32 i = 0; i < length; i += 2) {
        u16 word = (u16)data[i] << 8;
        if (i + 1 < length)
            word |= data[i + 1];
        sum += word;
        while (sum >> 16)
            sum = (sum & 0xffffU) + (sum >> 16);
    }
    return (u16)sum;
}

static inline u16 checksum16(u32 offset, void *buf, u16 len,
                             u32 pre_sum, int complement)
{
    (void)offset;
    u16 sum = net_checksum16((const u8 *)buf, len, pre_sum);
    return complement ? (u16)~sum : sum;
}

#endif
