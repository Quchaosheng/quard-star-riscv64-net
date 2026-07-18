#include <timeros/os.h>
#include <timeros/fatfs_port.h>
#include <ff.h>
#include <diskio.h>

DSTATUS disk_initialize(BYTE pdrv)
{
    return pdrv == 0 ? 0 : STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv)
{
    return pdrv == 0 ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0 || buff == 0 || count == 0)
        return RES_PARERR;
    return virtio_blk_transfer(buff, sector, count, 0) == 0 ?
           RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0 || buff == 0 || count == 0)
        return RES_PARERR;
    return virtio_blk_transfer((void *)buff, sector, count, 1) == 0 ?
           RES_OK : RES_ERROR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0)
        return RES_PARERR;

    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        if (buff == 0)
            return RES_PARERR;
        u64 sector_count = virtio_blk_sector_count();
        if (sector_count > 0xffffffffULL)
            return RES_PARERR;
        *(LBA_t *)buff = (LBA_t)sector_count;
        return RES_OK;
    case GET_SECTOR_SIZE:
        if (buff == 0)
            return RES_PARERR;
        *(WORD *)buff = FATFS_SECTOR_SIZE;
        return RES_OK;
    case GET_BLOCK_SIZE:
        if (buff == 0)
            return RES_PARERR;
        *(DWORD *)buff = 1;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}
