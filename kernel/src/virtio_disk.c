#include <timeros/os.h>

static struct disk {
    char pages[2 * PAGE_SIZE];
    struct virtio_mmio mmio;
    struct virtqueue queue;
    struct {
        struct buf *b;
        u8 status;
    } info[VIRTQ_NUM];
    struct virtio_blk_req ops[VIRTQ_NUM];
} __attribute__((aligned(PAGE_SIZE))) disk;

void virtio_disk_smoke_test(void)
{
    static struct buf b;

    b.blockno = 1;
    for (int i = 0; i < BSIZE; i++)
        b.data[i] = (char)(i ^ 0x5a);

    virtio_disk_rw(&b, 1);
    memset(b.data, 0, BSIZE);
    virtio_disk_rw(&b, 0);

    for (int i = 0; i < BSIZE; i++) {
        if ((u8)b.data[i] != (u8)(i ^ 0x5a)) {
            printk("QS:TEST_FAIL:block-readback\n");
            panic("virtio disk smoke test");
        }
    }
    printk("QS:BLOCK_OK\n");
}

void virtio_disk_init(void)
{
    u32 rejected = (1U << VIRTIO_BLK_F_RO) |
                   (1U << VIRTIO_BLK_F_SCSI) |
                   (1U << VIRTIO_BLK_F_CONFIG_WCE) |
                   (1U << VIRTIO_BLK_F_MQ) |
                   (1U << VIRTIO_F_ANY_LAYOUT) |
                   (1U << VIRTIO_RING_F_EVENT_IDX) |
                   (1U << VIRTIO_RING_F_INDIRECT_DESC);

    if (virtio_mmio_init(&disk.mmio, VIRTIO0, 2, rejected) < 0)
        panic("could not find virtio disk");
    if (virtio_mmio_setup_queue(&disk.mmio, 0, &disk.queue,
                                disk.pages) < 0)
        panic("could not configure virtio disk queue");
    virtio_mmio_driver_ok(&disk.mmio);
    printk("virtio_disk_init success !!! \n");
}

void virtio_disk_rw(struct buf *b, int write)
{
    u64 sector = b->blockno * (BSIZE / 512);
    int idx[3];

    while (virtq_alloc_chain(&disk.queue, 3, idx) < 0)
        schedule();

    struct virtio_blk_req *request = &disk.ops[idx[0]];
    request->type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    request->reserved = 0;
    request->sector = sector;

    disk.queue.desc[idx[0]].addr = (u64)(uintptr_t)request;
    disk.queue.desc[idx[0]].len = sizeof(*request);

    disk.queue.desc[idx[1]].addr = (u64)(uintptr_t)b->data;
    disk.queue.desc[idx[1]].len = BSIZE;
    if (!write)
        disk.queue.desc[idx[1]].flags |= VRING_DESC_F_WRITE;

    disk.info[idx[0]].status = 0xff;
    disk.queue.desc[idx[2]].addr =
        (u64)(uintptr_t)&disk.info[idx[0]].status;
    disk.queue.desc[idx[2]].len = 1;
    disk.queue.desc[idx[2]].flags |= VRING_DESC_F_WRITE;

    b->disk = 1;
    disk.info[idx[0]].b = b;
    virtq_submit(&disk.queue, (u16)idx[0]);
    virtio_mmio_notify(&disk.mmio, 0);

    struct buf volatile *pending = b;
    while (pending->disk == 1)
        virtio_disk_intr();

    disk.info[idx[0]].b = 0;
    if (virtq_free_chain(&disk.queue, (u16)idx[0]) != 3)
        panic("virtio disk descriptor chain");
}

void virtio_disk_intr(void)
{
    virtio_mmio_ack_interrupt(&disk.mmio);
    __sync_synchronize();

    for (;;) {
        u16 id;
        int result = virtq_pop_used(&disk.queue, &id);
        if (result == 0)
            return;
        if (result < 0)
            panic("virtio disk invalid used id");
        if (disk.info[id].b == 0 || disk.info[id].status != 0)
            panic("virtio disk completion status");
        disk.info[id].b->disk = 0;
    }
}
