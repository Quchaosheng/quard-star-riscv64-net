#include <timeros/os.h>

// 缓存区
struct {
	struct buf buf[NBUF]; //共30个缓冲块供上层使用
	struct buf head;
} bcache;

//
void binit()
{
	struct buf *b;
	// 初始化双向循环链表头尾指向自身
	bcache.head.prev = &bcache.head;
	bcache.head.next = &bcache.head;
    // 头插法初始化好双向循环链表
	for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
		b->next = bcache.head.next;
		b->prev = &bcache.head;
		bcache.head.next->prev = b;
		bcache.head.next = b;
	}
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// 从bcache中获取缓冲块
static struct buf *bget(int dev, int blockno)
{
	struct buf *b;
	// Is the block already cached?
	for (b = bcache.head.next; b != &bcache.head; b = b->next) {
		if (b->dev == dev && b->blockno == blockno) { ///如果设备和块号都对上，那么是要找的块
			b->refcnt++; //该块的引用加1
			return b;    //返回该块
		}
	}
	// Not cached.
	// Recycle the least recently used (LRU) unused buffer.
	for (b = bcache.head.prev; b != &bcache.head; b = b->prev) {
		if (b->refcnt == 0) {
			b->dev = dev;
			b->blockno = blockno;
			b->valid = 0;  //刚分配的缓存块，数据无效
			b->refcnt = 1; //引用数为1
			return b;      //返回该缓存块
		}
	}
	panic("bget: no buffers");
	return 0;
}

const int R = 0;
const int W = 1;

// Return a buf with the contents of the indicated block.
// 返回一个存在有效数据的缓存块
struct buf *bread(int dev, int blockno)
{
	struct buf *b;
	b = bget(dev, blockno);
	if (!b->valid) {
		virtio_disk_rw(b, R);
		b->valid = 1;
	}
	return b;
}

// Write b's contents to disk.
// 将缓存块写到相应磁盘块
void bwrite(struct buf *b)
{
	virtio_disk_rw(b, W);
}

// Release a buffer.
// Move to the head of the most-recently-used list.
void brelse(struct buf *b)
{
	b->refcnt--;
	if (b->refcnt == 0) {
		// no one is waiting for it.
		b->next->prev = b->prev;
		b->prev->next = b->next;
		b->next = bcache.head.next;
		b->prev = &bcache.head;
		bcache.head.next->prev = b;
		bcache.head.next = b;
	}
}

void bpin(struct buf *b)
{
	b->refcnt++;
}

void bunpin(struct buf *b)
{
	b->refcnt--;
}
