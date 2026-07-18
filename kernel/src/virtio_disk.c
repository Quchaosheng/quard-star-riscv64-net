#include <timeros/os.h>

#ifndef QS_BLOCK_ITERATIONS
#define QS_BLOCK_ITERATIONS 16
#endif

struct block_waiter {
    struct wait_queue completion;
    int done;
    int result;
};

struct block_info {
    struct virtio_blk_req request;
    u8 status;
    struct block_waiter *waiter;
};

static struct disk {
    char pages[2 * PAGE_SIZE];
    struct virtio_mmio mmio;
    struct virtqueue queue;
    struct spinlock lock;
    struct wait_queue descriptors;
    struct block_info info[VIRTQ_NUM];
    int pending;
    int failed;
    u32 irq_reported;
    u64 capacity;
} __attribute__((aligned(PAGE_SIZE))) disk;

static void block_fail_locked(const char *code)
{
    if (!disk.failed)
        printk("QS:TEST_FAIL:m3-block:%s\n", code);
    disk.failed = 1;

    for (int i = 0; i < VIRTQ_NUM; i++) {
        struct block_waiter *waiter = disk.info[i].waiter;
        if (waiter == 0)
            continue;
        waiter->result = -1;
        waiter->done = 1;
        disk.info[i].waiter = 0;
        task_wake(&waiter->completion, 1);
    }
    disk.pending = 0;
    task_wake(&disk.descriptors, 1);
}

static int block_transfer(void *data, u64 sector, u32 bytes, int write)
{
    struct block_waiter waiter;
    int idx[3];

    wait_queue_init(&waiter.completion);
    waiter.done = 0;
    waiter.result = -1;

    spin_lock(&disk.lock);
    while (!disk.failed && virtq_alloc_chain(&disk.queue, 3, idx) < 0)
        task_sleep(&disk.descriptors, &disk.lock, WAIT_FOREVER);
    if (disk.failed) {
        spin_unlock(&disk.lock);
        return -1;
    }

    struct block_info *info = &disk.info[idx[0]];
    info->request.type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    info->request.reserved = 0;
    info->request.sector = sector;
    info->status = 0xff;
    info->waiter = &waiter;

    disk.queue.desc[idx[0]].addr = (u64)(uintptr_t)&info->request;
    disk.queue.desc[idx[0]].len = sizeof(info->request);
    disk.queue.desc[idx[1]].addr = (u64)(uintptr_t)data;
    disk.queue.desc[idx[1]].len = bytes;
    if (!write)
        disk.queue.desc[idx[1]].flags |= VRING_DESC_F_WRITE;
    disk.queue.desc[idx[2]].addr = (u64)(uintptr_t)&info->status;
    disk.queue.desc[idx[2]].len = 1;
    disk.queue.desc[idx[2]].flags |= VRING_DESC_F_WRITE;

    disk.pending++;
    virtq_submit(&disk.queue, (u16)idx[0]);
    virtio_mmio_notify(&disk.mmio, 0);
    while (!waiter.done)
        task_sleep(&waiter.completion, &disk.lock, WAIT_FOREVER);

    int result = waiter.result;
    spin_unlock(&disk.lock);
    return result;
}

void virtio_disk_smoke_test(void)
{
    static struct buf b;
    int free_baseline = virtq_free_count(&disk.queue);

    for (int iteration = 0; iteration < QS_BLOCK_ITERATIONS; iteration++) {
        b.blockno = 1 + (iteration % 8);
        for (int i = 0; i < BSIZE; i++)
            b.data[i] = (char)(iteration ^ i ^ 0x5a);

        if (virtio_blk_transfer(b.data, b.blockno * (BSIZE / 512),
                                BSIZE / 512, 1) < 0)
            panic("virtio disk smoke write");
        memset(b.data, 0, BSIZE);
        if (virtio_blk_transfer(b.data, b.blockno * (BSIZE / 512),
                                BSIZE / 512, 0) < 0)
            panic("virtio disk smoke read");

        for (int i = 0; i < BSIZE; i++) {
            if ((u8)b.data[i] != (u8)(iteration ^ i ^ 0x5a)) {
                printk("QS:TEST_FAIL:block-readback\n");
                panic("virtio disk smoke test");
            }
        }
    }

    spin_lock(&disk.lock);
    if (disk.pending != 0 || virtq_free_count(&disk.queue) != free_baseline) {
        spin_unlock(&disk.lock);
        printk("QS:TEST_FAIL:m3-block:leak\n");
        panic("virtio disk descriptor leak");
    }
    spin_unlock(&disk.lock);

    printk("QS:VIRTQUEUE_OK\n");
    printk("QS:BLOCK_STRESS_OK\n");
    printk("QS:BLOCK_OK\n");
    m3_mark_virtqueue();
    m3_mark_block_stress();
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

    if (virtio_mmio_init(&disk.mmio, VIRTIO0, 2, rejected) < 0) {
        printk("QS:TEST_FAIL:m3-block:init\n");
        panic("could not find virtio disk");
    }
    if (virtio_mmio_setup_queue(&disk.mmio, 0, &disk.queue,
                                disk.pages) < 0) {
        printk("QS:TEST_FAIL:m3-block:queue\n");
        panic("could not configure virtio disk queue");
    }
    spin_init(&disk.lock);
    wait_queue_init(&disk.descriptors);
    disk.pending = 0;
    disk.failed = 0;
    disk.irq_reported = 0;
    disk.capacity = virtio_mmio_config64(&disk.mmio, 0);
    if (disk.capacity == 0) {
        printk("QS:TEST_FAIL:m3-block:capacity\n");
        panic("virtio disk capacity");
    }
    for (int i = 0; i < VIRTQ_NUM; i++)
        disk.info[i].waiter = 0;
    virtio_mmio_driver_ok(&disk.mmio);
    printk("virtio_disk_init success !!! \n");
}

void virtio_disk_rw(struct buf *b, int write)
{
    if (virtio_blk_transfer(b->data, b->blockno * (BSIZE / 512),
                            BSIZE / 512, write) < 0)
        panic("virtio disk I/O");
}

int virtio_blk_transfer(void *data, u64 sector, u32 count, int write)
{
    if (data == 0 || count == 0 || count > (~0U / 512U))
        return -1;
    if (sector >= disk.capacity || count > disk.capacity - sector)
        return -1;
    return block_transfer(data, sector, count * 512U, write);
}

u64 virtio_blk_sector_count(void)
{
    return disk.capacity;
}

int virtio_blk_free_descriptors(void)
{
    spin_lock(&disk.lock);
    int count = virtq_free_count(&disk.queue);
    spin_unlock(&disk.lock);
    return count;
}

int virtio_blk_pending_requests(void)
{
    spin_lock(&disk.lock);
    int pending = disk.pending;
    spin_unlock(&disk.lock);
    return pending;
}

void virtio_disk_intr(void)
{
    virtio_mmio_ack_interrupt(&disk.mmio);
    spin_lock(&disk.lock);

    for (;;) {
        u16 id;
        int result = virtq_pop_used(&disk.queue, &id);
        if (result == 0)
            break;
        if (result < 0) {
            block_fail_locked("used-id");
            break;
        }

        struct block_waiter *waiter = disk.info[id].waiter;
        if (waiter == 0) {
            block_fail_locked("duplicate");
            break;
        }
        waiter->result = disk.info[id].status == 0 ? 0 : -1;
        waiter->done = 1;
        if (virtq_free_chain(&disk.queue, id) != 3) {
            block_fail_locked("chain");
            break;
        }
        disk.info[id].waiter = 0;
        disk.pending--;

        if (!__atomic_exchange_n(&disk.irq_reported, 1,
                                 __ATOMIC_ACQ_REL)) {
            printk("QS:BLOCK_IRQ_OK\n");
            m3_mark_block_irq();
        }
        task_wake(&waiter->completion, 1);
        task_wake(&disk.descriptors, 1);
    }
    spin_unlock(&disk.lock);
}
