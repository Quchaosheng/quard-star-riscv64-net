#ifndef TOS_VIRTIO_MMIO_H__
#define TOS_VIRTIO_MMIO_H__

#include <timeros/types.h>
#include <timeros/virtqueue.h>

#define VIRTIO_MMIO_MAGIC_VALUE       0x000
#define VIRTIO_MMIO_VERSION           0x004
#define VIRTIO_MMIO_DEVICE_ID         0x008
#define VIRTIO_MMIO_VENDOR_ID         0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES   0x010
#define VIRTIO_MMIO_DRIVER_FEATURES   0x020
#define VIRTIO_MMIO_GUEST_PAGE_SIZE   0x028
#define VIRTIO_MMIO_QUEUE_SEL         0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX     0x034
#define VIRTIO_MMIO_QUEUE_NUM         0x038
#define VIRTIO_MMIO_QUEUE_ALIGN       0x03c
#define VIRTIO_MMIO_QUEUE_PFN         0x040
#define VIRTIO_MMIO_QUEUE_NOTIFY      0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS  0x060
#define VIRTIO_MMIO_INTERRUPT_ACK     0x064
#define VIRTIO_MMIO_STATUS            0x070
#define VIRTIO_MMIO_CONFIG            0x100

#define VIRTIO_CONFIG_S_ACKNOWLEDGE 1
#define VIRTIO_CONFIG_S_DRIVER      2
#define VIRTIO_CONFIG_S_DRIVER_OK   4
#define VIRTIO_CONFIG_S_FEATURES_OK 8

struct virtio_mmio {
    u64 base;
    u32 device_id;
};

int virtio_mmio_init(struct virtio_mmio *dev, u64 base, u32 device_id,
                     u32 rejected_features);
int virtio_mmio_setup_queue(struct virtio_mmio *dev, u16 queue,
                            struct virtqueue *vq, void *pages);
void virtio_mmio_driver_ok(struct virtio_mmio *dev);
void virtio_mmio_notify(struct virtio_mmio *dev, u16 queue);
u32 virtio_mmio_ack_interrupt(struct virtio_mmio *dev);
u64 virtio_mmio_config64(struct virtio_mmio *dev, u32 offset);

#endif
