#ifndef TOS_BUF_H
#define TOS_BUF_H

#define MAXOPBLOCKS 10 // max # of blocks any FS op writes
#define NBUF (MAXOPBLOCKS * 3) // size of disk block cache
#define BSIZE 1024 // block size

// 缓存块
struct buf {
	int valid; // 记录是否数据从磁盘读取到内存，首次记录目标块时会标记为0
	int disk; // does disk "own" buf?
	int dev;  // 设备号
	int blockno; // 缓冲对应的硬盘块号
	int refcnt;  //记录有多少进程在使用该缓冲块
	struct buf *prev; // LRU cache list
	struct buf *next;
	char data[BSIZE]; //硬盘数据存储位置
};

void binit(void);

// 为某个特定磁盘块获取一块缓冲
struct buf *bread(int, int);

// 对缓冲操作结束，释放缓冲
void brelse(struct buf *);

// 对缓冲中数据修改完之后，将它写入硬盘
void bwrite(struct buf *);

void bpin(struct buf *);

void bunpin(struct buf *);

#endif