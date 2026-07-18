#ifndef TOS_VIRTIO_H__
#define TOS_VIRTIO_H__

#include <timeros/bio.h>
#include <timeros/virtio_mmio.h>
#include <timeros/virtqueue.h>

// quard_star的virtio起始地址
#define VIRTIO0 0x10100000


// device feature bits
#define VIRTIO_BLK_F_RO 5 /* Disk is read-only */
#define VIRTIO_BLK_F_SCSI 7 /* Supports scsi command passthru */
#define VIRTIO_BLK_F_CONFIG_WCE 11 /* Writeback mode available in config */
#define VIRTIO_BLK_F_MQ 12 /* support more than one vq */
#define VIRTIO_F_ANY_LAYOUT 27
#define VIRTIO_RING_F_INDIRECT_DESC 28
#define VIRTIO_RING_F_EVENT_IDX 29

// this many virtio descriptors.
// must be a power of two.
#define NUM VIRTQ_NUM

// these are specific to virtio block devices, e.g. disks,
// described in Section 5.2 of the spec.

#define VIRTIO_BLK_T_IN 0 // read the disk   读磁盘
#define VIRTIO_BLK_T_OUT 1 // write the disk 写磁盘

// the format of the first descriptor in a disk request.
// to be followed by two more descriptors containing
// the block, and a one-byte status.
struct virtio_blk_req {
	u32 type; // VIRTIO_BLK_T_IN or ..._OUT
	u32 reserved;
	u64 sector;
};


// 初始化virtio
void virtio_disk_init();
void virtio_disk_smoke_test();

// 磁盘读写
void virtio_disk_rw(struct buf *, int );
void virtio_disk_intr();

#endif
