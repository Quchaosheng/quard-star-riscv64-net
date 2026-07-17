#include <timeros/os.h>

#define VTMO_REG(r) ((volatile uint32_t *)(VIRTIO0 + (r)))

static struct disk
{
    // 这两页内存用于存放 (descriptors, avail,used） 这三个队列
    // 没有像xv6使用kalloc来分配两页连续内存
    // 这两页内存被分成了三个部分即：(descriptors, avail,used）

    char pages[2 * PAGE_SIZE];

    // 总共有 NUM 个descriptor，
    struct virtq_desc *desc;


    struct virtq_avail *avail;

    struct virtq_used *used;

    char free[NUM];

    u16 used_idx;

    struct {
		struct buf *b;
		char status;
	} info[NUM];

    struct virtio_blk_req ops[NUM];
}__attribute__((aligned(PAGE_SIZE))) disk;

void virtio_disk_smoke_test()
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

void virtio_disk_init(){

    u32 status = 0;

    // 做一些检查操作，判断MMIO相关寄存器没有问题
    if(*VTMO_REG(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
       *VTMO_REG(VIRTIO_MMIO_VERSION) != 1 ||
       *VTMO_REG(VIRTIO_MMIO_DEVICE_ID) != 2 ||
       *VTMO_REG(VIRTIO_MMIO_VENDOR_ID) != 0x554d4551){
        panic("could not find virtio disk");
    }

    // 第1步 重置设备
    *VTMO_REG(VIRTIO_MMIO_STATUS) = status;

    // 第2步 设置 Satus 寄存器第0位 os识别到设备
    status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
	*VTMO_REG(VIRTIO_MMIO_STATUS) = status;

    // 第3步 设置 Satus 寄存器第1位 os知道如何驱动设备
	status |= VIRTIO_CONFIG_S_DRIVER;
	*VTMO_REG(VIRTIO_MMIO_STATUS) = status;

    // 第4步 协商 feature
    u64 features = *VTMO_REG(VIRTIO_MMIO_DEVICE_FEATURES);
    features &= ~(1 << VIRTIO_BLK_F_RO);
	features &= ~(1 << VIRTIO_BLK_F_SCSI);
	features &= ~(1 << VIRTIO_BLK_F_CONFIG_WCE);
	features &= ~(1 << VIRTIO_BLK_F_MQ);
	features &= ~(1 << VIRTIO_F_ANY_LAYOUT);
	features &= ~(1 << VIRTIO_RING_F_EVENT_IDX);
	features &= ~(1 << VIRTIO_RING_F_INDIRECT_DESC);
    *VTMO_REG(VIRTIO_MMIO_DRIVER_FEATURES) = features;

    // 第5步 协商完成
    status |= VIRTIO_CONFIG_S_FEATURES_OK;
    *VTMO_REG(VIRTIO_MMIO_STATUS) = status;

    *VTMO_REG(VIRTIO_MMIO_GUEST_PAGE_SIZE) = PAGE_SIZE;

    // 第6步 确保FEATURES_OK
    status = *VTMO_REG(VIRTIO_MMIO_STATUS);
    if(!(status & VIRTIO_CONFIG_S_FEATURES_OK))
        panic("virtio disk FEATURES_OK unset");

    // 第7步 initialize queue 0.
	*VTMO_REG(VIRTIO_MMIO_QUEUE_SEL) = 0;

    // check maximum queue size.
    u32 max = *VTMO_REG(VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (max == 0)
        panic("virtio disk has no queue 0");
    if (max < NUM)
        panic("virtio disk max queue too short");
    *VTMO_REG(VIRTIO_MMIO_QUEUE_NUM) = NUM;

    // desc = pages -- num * virtq_desc
	// avail = pages + 0x40 -- 2 * uint16, then num * uint16
	// used = pages + 4096 -- 2 * uint16, then num * vRingUsedElem

	disk.desc = (struct virtq_desc *)disk.pages;
	disk.avail = (struct virtq_avail *)(disk.pages +
					    NUM * sizeof(struct virtq_desc));
	disk.used = (struct virtq_used *)(disk.pages + PAGE_SIZE);
	*VTMO_REG(VIRTIO_MMIO_QUEUE_ALIGN) = PAGE_SIZE;
	*VTMO_REG(VIRTIO_MMIO_QUEUE_PFN) = (u32)((u64)disk.pages >> PAGE_SIZE_BITS);

	// all NUM descriptors start out unused.
	for (int i = 0; i < NUM; i++)
		disk.free[i] = 1;


    // Set the DRIVER status bit: the guest OS knows how to drive the device.
    status |= VIRTIO_CONFIG_S_DRIVER_OK;
    *VTMO_REG(VIRTIO_MMIO_STATUS) = status;

    printk("virtio_disk_init success !!! \n");
}

// 找到一个空闲的descriptor，将其置为被使用状态，返回其索引
static int alloc_desc()
{
	for (int i = 0; i < NUM; i++) {
		if (disk.free[i]) {
			disk.free[i] = 0;
			return i;
		}
	}
	return -1;
}

// 根据descriptor的索引将其置为空闲状态
static void free_desc(int i)
{
	if (i >= NUM)
		panic("free_desc 1");
	if (disk.free[i])
		panic("free_desc 2");
	disk.desc[i].addr = 0;
	disk.desc[i].len = 0;
	disk.desc[i].flags = 0;
	disk.desc[i].next = 0;
	disk.free[i] = 1;
}

// free a chain of descriptors.
static void free_chain(int i)
{
	while (1) {
		int flag = disk.desc[i].flags;
		int nxt = disk.desc[i].next;
		free_desc(i);
		if (flag & VRING_DESC_F_NEXT)
			i = nxt;
		else
			break;
	}
}

// 分配三个空闲的descriptor
// 磁盘传输通常需要使用三个描述符来完成相关操作
static int alloc3_desc(int *idx)
{
	for (int i = 0; i < 3; i++) {
		idx[i] = alloc_desc();
		if (idx[i] < 0) {
			for (int j = 0; j < i; j++)
				free_desc(idx[j]);
			return -1;
		}
	}
	return 0;
}

extern int PID;

void virtio_disk_rw(struct buf *b, int write)
{
    u64 sector = b->blockno * (BSIZE / 512);

    int idx[3];
    while (1)
    {
        if(alloc3_desc(idx) == 0){
            break;
        }
        schedule();
    }
    //初始化请求头，使用disk的ops域存储，ops存储位置和第一个描述符的位置一致
    struct virtio_blk_req *buf0 = &disk.ops[idx[0]];

    if(write)
        buf0->type = VIRTIO_BLK_T_OUT; // 写磁盘，初始化请求头
    else
        buf0->type = VIRTIO_BLK_T_IN;  // 读磁盘，初始化请求头

    buf0->reserved = 0;
    buf0->sector = sector;

    // 用请求头初始化第一个descriptor
    disk.desc[idx[0]].addr = (u64)buf0;
    disk.desc[idx[0]].len = sizeof(struct virtio_blk_req);
	disk.desc[idx[0]].flags = VRING_DESC_F_NEXT;
	disk.desc[idx[0]].next = idx[1];

    //用实际数据初始化第二个descriptor
    disk.desc[idx[1]].addr = (u64) b->data;
    disk.desc[idx[1]].len = BSIZE;
    if(write)
        disk.desc[idx[1]].flags = 0; // device reads b->data
    else
        disk.desc[idx[1]].flags = VRING_DESC_F_WRITE; // device writes b->data
    disk.desc[idx[1]].flags |= VRING_DESC_F_NEXT;
    disk.desc[idx[1]].next = idx[2];

    disk.info[idx[0]].status = 0xff; // device writes 0 on success

    //第三个描述符的特殊操作，待设备写入的status放在disk.info域
    disk.desc[idx[2]].addr = (u64) &disk.info[idx[0]].status;
    disk.desc[idx[2]].len = 1;
    disk.desc[idx[2]].flags = VRING_DESC_F_WRITE; // device writes the status
    disk.desc[idx[2]].next = 0;

    // 记录struct buf信息，方便中断处理程序处理
    b->disk = 1;
    disk.info[idx[0]].b = b;

    // 把有效index写入avail ring中
	disk.avail->ring[disk.avail->idx % NUM] = idx[0];

    __sync_synchronize();

    disk.avail->idx += 1; // not % NUM ...

    __sync_synchronize();

    *VTMO_REG(VIRTIO_MMIO_QUEUE_NOTIFY) = 0; // value is queue number

    struct buf volatile *_b = b;
    while (_b->disk == 1) {
        virtio_disk_intr();
    }
	disk.info[idx[0]].b = 0;
	free_chain(idx[0]);
}

// 中断处理函数
void virtio_disk_intr()
{
    *VTMO_REG(VIRTIO_MMIO_INTERRUPT_ACK) = *VTMO_REG(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;

    __sync_synchronize();

    // 中断响应
    while (disk.used_idx != disk.used->idx) {
		__sync_synchronize();
		int id = disk.used->ring[disk.used_idx % NUM].id;

		if (disk.info[id].status != 0)
			panic("virtio_disk_intr status");

		struct buf *b = disk.info[id].b;
		b->disk = 0; // disk is done with buf
		disk.used_idx += 1;
	}
}
