#ifndef TOS_VIRTIO_NET_H__
#define TOS_VIRTIO_NET_H__

#include <timeros/types.h>

#define VIRTIO_NET_DEVICE_ID 1
#define VIRTIO_NET_RX_QUEUE 0
#define VIRTIO_NET_TX_QUEUE 1
#define VIRTIO_NET_HDR_SIZE 10

#define VIRTIO_NET_F_CSUM 0
#define VIRTIO_NET_F_GUEST_CSUM 1
#define VIRTIO_NET_F_MAC 5
#define VIRTIO_NET_F_GSO 6
#define VIRTIO_NET_F_GUEST_TSO4 7
#define VIRTIO_NET_F_GUEST_TSO6 8
#define VIRTIO_NET_F_GUEST_ECN 9
#define VIRTIO_NET_F_GUEST_UFO 10
#define VIRTIO_NET_F_HOST_TSO4 11
#define VIRTIO_NET_F_HOST_TSO6 12
#define VIRTIO_NET_F_HOST_ECN 13
#define VIRTIO_NET_F_HOST_UFO 14
#define VIRTIO_NET_F_MRG_RXBUF 15
#define VIRTIO_NET_F_STATUS 16
#define VIRTIO_NET_F_CTRL_VQ 17
#define VIRTIO_NET_F_CTRL_RX 18
#define VIRTIO_NET_F_CTRL_VLAN 19
#define VIRTIO_NET_F_GUEST_ANNOUNCE 21
#define VIRTIO_NET_F_MQ 22
#define VIRTIO_NET_F_CTRL_MAC_ADDR 23

#define ETHERNET_HEADER_SIZE 14
#define ETHERNET_MIN_FRAME 60
#define ETHERNET_MAX_FRAME 1514

struct virtio_net_hdr {
    u8 flags;
    u8 gso_type;
    u16 hdr_len;
    u16 gso_size;
    u16 csum_start;
    u16 csum_offset;
} __attribute__((packed));

struct virtio_net_stats {
    u64 rx_packets;
    u64 tx_packets;
    u64 rx_dropped;
    u64 tx_errors;
    u64 interrupts;
    u64 resets;
};

int virtio_net_init(void);
int virtio_net_get_mac(u8 *mac);
int virtio_net_send(const void *frame, u32 length);
int virtio_net_receive(void *frame, u32 capacity, u32 *length,
                       u64 deadline);
int virtio_net_reset(void);
void virtio_net_get_stats(struct virtio_net_stats *stats);
int virtio_net_free_tx_slots(void);
int virtio_net_posted_rx_buffers(void);
int virtio_net_free_tx_descriptors(void);
int virtio_net_pending_tx(void);
int virtio_net_rx_completions(void);
int virtio_net_raw_test(void);
void virtio_net_intr(void);

#endif
