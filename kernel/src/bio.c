#include <timeros/os.h>

struct {
	struct sleeplock lock;
	struct buf buf[NBUF];
	struct buf head;
} bcache;

void binit(void)
{
	struct buf *b;

	sleeplock_init(&bcache.lock);
	bcache.head.prev = &bcache.head;
	bcache.head.next = &bcache.head;
	for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
		sleeplock_init(&b->data_lock);
		b->next = bcache.head.next;
		b->prev = &bcache.head;
		bcache.head.next->prev = b;
		bcache.head.next = b;
	}
}

static struct buf *bget(int dev, int blockno)
{
	struct buf *b;

	sleeplock_acquire(&bcache.lock);
	for (b = bcache.head.next; b != &bcache.head; b = b->next) {
		if (b->dev == dev && b->blockno == blockno) {
			b->refcnt++;
			sleeplock_release(&bcache.lock);
			sleeplock_acquire(&b->data_lock);
			return b;
		}
	}

	for (b = bcache.head.prev; b != &bcache.head; b = b->prev) {
		if (b->refcnt == 0) {
			b->dev = dev;
			b->blockno = blockno;
			b->valid = 0;
			b->refcnt = 1;
			sleeplock_release(&bcache.lock);
			sleeplock_acquire(&b->data_lock);
			return b;
		}
	}

	sleeplock_release(&bcache.lock);
	panic("bget: no buffers");
	return 0;
}

const int R = 0;
const int W = 1;

struct buf *bread(int dev, int blockno)
{
	struct buf *b = bget(dev, blockno);

	if (!b->valid) {
		virtio_disk_rw(b, R);
		b->valid = 1;
	}
	return b;
}

void bwrite(struct buf *b)
{
	virtio_disk_rw(b, W);
}

void brelse(struct buf *b)
{
	sleeplock_release(&b->data_lock);
	sleeplock_acquire(&bcache.lock);
	b->refcnt--;
	if (b->refcnt == 0) {
		b->next->prev = b->prev;
		b->prev->next = b->next;
		b->next = bcache.head.next;
		b->prev = &bcache.head;
		bcache.head.next->prev = b;
		bcache.head.next = b;
	}
	sleeplock_release(&bcache.lock);
}

void bpin(struct buf *b)
{
	sleeplock_acquire(&bcache.lock);
	b->refcnt++;
	sleeplock_release(&bcache.lock);
}

void bunpin(struct buf *b)
{
	sleeplock_acquire(&bcache.lock);
	b->refcnt--;
	sleeplock_release(&bcache.lock);
}
