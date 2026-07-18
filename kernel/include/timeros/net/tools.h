#ifndef TOS_NET_TOOLS_H__
#define TOS_NET_TOOLS_H__

#include <timeros/net/net_port.h>
#include <timeros/net/net_err.h>

static inline u16 swap_u16(u16 value)
{
    return (u16)((value << 8) | (value >> 8));
}

static inline u32 swap_u32(u32 value)
{
    return ((value & 0x000000ffU) << 24) |
           ((value & 0x0000ff00U) << 8) |
           ((value & 0x00ff0000U) >> 8) |
           ((value & 0xff000000U) >> 24);
}

#if NET_ENDIAN_LITTLE
#define x_htons(value) swap_u16((u16)(value))
#define x_ntohs(value) swap_u16((u16)(value))
#define x_htonl(value) swap_u32((u32)(value))
#define x_ntohl(value) swap_u32((u32)(value))
#else
#define x_htons(value) ((u16)(value))
#define x_ntohs(value) ((u16)(value))
#define x_htonl(value) ((u32)(value))
#define x_ntohl(value) ((u32)(value))
#endif

static inline u32 net_checksum_fold(u32 sum)
{
    while (sum >> 16)
        sum = (sum & 0xffffU) + (sum >> 16);
    return sum;
}

static inline u16 checksum16(u32 offset, void *buf, u16 len,
                             u32 pre_sum, int complement)
{
    const u8 *data = (const u8 *)buf;
    u32 sum = pre_sum;

    for (u32 i = 0; i < len; i++) {
        if (((offset + i) & 1U) == 0)
            sum += (u32)data[i] << 8;
        else
            sum += data[i];
        sum = net_checksum_fold(sum);
    }
    return complement ? (u16) ~sum : (u16)sum;
}

static inline u16 net_checksum16(const u8 *data, u32 length, u32 sum)
{
    return checksum16(0, (void *)data, (u16)length, sum, 0);
}

net_err_t tools_init(void);

#endif
