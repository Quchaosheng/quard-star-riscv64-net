#include <timeros/virtio_mmio.h>

static volatile u32 *mmio_reg(struct virtio_mmio *dev, u32 offset)
{
    return (volatile u32 *)(uintptr_t)(dev->base + offset);
}

int virtio_mmio_init(struct virtio_mmio *dev, u64 base, u32 device_id,
                     u32 rejected_features, u32 required_features,
                     u32 *negotiated_features)
{
    if (dev == 0)
        return -1;
    dev->base = base;
    dev->device_id = device_id;

    if (*mmio_reg(dev, VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
        *mmio_reg(dev, VIRTIO_MMIO_VERSION) != 1 ||
        *mmio_reg(dev, VIRTIO_MMIO_DEVICE_ID) != device_id ||
        *mmio_reg(dev, VIRTIO_MMIO_VENDOR_ID) != 0x554d4551)
        return -1;

    u32 status = 0;
    *mmio_reg(dev, VIRTIO_MMIO_STATUS) = status;
    status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
    *mmio_reg(dev, VIRTIO_MMIO_STATUS) = status;
    status |= VIRTIO_CONFIG_S_DRIVER;
    *mmio_reg(dev, VIRTIO_MMIO_STATUS) = status;

    u32 features = *mmio_reg(dev, VIRTIO_MMIO_DEVICE_FEATURES);
    if ((features & required_features) != required_features)
        return -1;
    features &= ~rejected_features;
    *mmio_reg(dev, VIRTIO_MMIO_DRIVER_FEATURES) = features;

    status |= VIRTIO_CONFIG_S_FEATURES_OK;
    *mmio_reg(dev, VIRTIO_MMIO_STATUS) = status;
    if ((*mmio_reg(dev, VIRTIO_MMIO_STATUS) &
         VIRTIO_CONFIG_S_FEATURES_OK) == 0)
        return -1;
    if (negotiated_features != 0)
        *negotiated_features = features;
    return 0;
}

int virtio_mmio_setup_queue(struct virtio_mmio *dev, u16 queue,
                            struct virtqueue *vq, void *pages)
{
    if (dev == 0 || vq == 0 || pages == 0)
        return -1;

    *mmio_reg(dev, VIRTIO_MMIO_GUEST_PAGE_SIZE) = VIRTQ_PAGE_SIZE;
    *mmio_reg(dev, VIRTIO_MMIO_QUEUE_SEL) = queue;
    u32 max = *mmio_reg(dev, VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (max < VIRTQ_NUM)
        return -1;
    if (virtq_init(vq, pages, VIRTQ_NUM) < 0)
        return -1;

    *mmio_reg(dev, VIRTIO_MMIO_QUEUE_NUM) = VIRTQ_NUM;
    *mmio_reg(dev, VIRTIO_MMIO_QUEUE_ALIGN) = VIRTQ_PAGE_SIZE;
    *mmio_reg(dev, VIRTIO_MMIO_QUEUE_PFN) =
        (u32)((u64)(uintptr_t)pages >> 12);
    return 0;
}

void virtio_mmio_driver_ok(struct virtio_mmio *dev)
{
    u32 status = *mmio_reg(dev, VIRTIO_MMIO_STATUS);
    *mmio_reg(dev, VIRTIO_MMIO_STATUS) = status | VIRTIO_CONFIG_S_DRIVER_OK;
}

void virtio_mmio_notify(struct virtio_mmio *dev, u16 queue)
{
    *mmio_reg(dev, VIRTIO_MMIO_QUEUE_NOTIFY) = queue;
}

u32 virtio_mmio_ack_interrupt(struct virtio_mmio *dev)
{
    u32 pending = *mmio_reg(dev, VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;
    *mmio_reg(dev, VIRTIO_MMIO_INTERRUPT_ACK) = pending;
    return pending;
}

u64 virtio_mmio_config64(struct virtio_mmio *dev, u32 offset)
{
    u64 low = *mmio_reg(dev, VIRTIO_MMIO_CONFIG + offset);
    u64 high = *mmio_reg(dev, VIRTIO_MMIO_CONFIG + offset + sizeof(u32));
    return low | (high << 32);
}

u8 virtio_mmio_config8(struct virtio_mmio *dev, u32 offset)
{
    volatile u8 *config = (volatile u8 *)(uintptr_t)
                          (dev->base + VIRTIO_MMIO_CONFIG + offset);
    return *config;
}

void virtio_mmio_reset(struct virtio_mmio *dev)
{
    *mmio_reg(dev, VIRTIO_MMIO_STATUS) = 0;
    __sync_synchronize();
}
