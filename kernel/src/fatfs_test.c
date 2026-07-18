#include <timeros/os.h>
#include <timeros/fatfs_test.h>
#include <ff.h>

#ifndef QS_FATFS_ITERATIONS
#define QS_FATFS_ITERATIONS 4
#endif

#define FATFS_TEST_SIZE 4096U
#define FATFS_VERIFY_ERROR 0x100
#define FATFS_COUNT_ERROR  0x101
#define FATFS_LEAK_ERROR   0x102

static FATFS fs;
static FIL file;
static u8 mkfs_work[FATFS_TEST_SIZE];
static u8 write_buffer[FATFS_TEST_SIZE];
static u8 read_buffer[FATFS_TEST_SIZE];

static int prepare_filesystem(void)
{
    FRESULT result = f_mount(&fs, "0:", 1);
    if (result != FR_NO_FILESYSTEM)
        return result;

    MKFS_PARM options = { FM_ANY | FM_SFD, 1, 0, 0, 0 };
    result = f_mkfs("0:", &options, mkfs_work, sizeof(mkfs_work));
    if (result != FR_OK)
        return result;
    return f_mount(&fs, "0:", 1);
}

static int verify_iteration(int iteration)
{
    UINT transferred;
    FRESULT result;

    for (UINT offset = 0; offset < FATFS_TEST_SIZE; offset++)
        write_buffer[offset] = (u8)(iteration ^ offset ^ 0x5a);

    result = f_open(&file, "0:/m3.bin", FA_WRITE | FA_CREATE_ALWAYS);
    if (result != FR_OK)
        return result;
    result = f_write(&file, write_buffer, FATFS_TEST_SIZE, &transferred);
    if (result == FR_OK && transferred != FATFS_TEST_SIZE)
        result = FATFS_COUNT_ERROR;
    if (result == FR_OK)
        result = f_sync(&file);
    FRESULT close_result = f_close(&file);
    if (result == FR_OK)
        result = close_result;
    if (result != FR_OK)
        return result;

    memset(read_buffer, 0, sizeof(read_buffer));
    result = f_open(&file, "0:/m3.bin", FA_READ);
    if (result != FR_OK)
        return result;
    result = f_read(&file, read_buffer, FATFS_TEST_SIZE, &transferred);
    if (result == FR_OK && transferred != FATFS_TEST_SIZE)
        result = FATFS_COUNT_ERROR;
    close_result = f_close(&file);
    if (result == FR_OK)
        result = close_result;
    if (result != FR_OK)
        return result;
    if (memcmp(write_buffer, read_buffer, FATFS_TEST_SIZE) != 0)
        return FATFS_VERIFY_ERROR;
    return FR_OK;
}

int fatfs_test_run(void)
{
    int free_baseline = virtio_blk_free_descriptors();
    int result = prepare_filesystem();

    if (result != FR_OK)
        return result;
    for (int iteration = 0; iteration < QS_FATFS_ITERATIONS; iteration++) {
        result = verify_iteration(iteration);
        if (result != FR_OK)
            return result;
    }
    if (virtio_blk_pending_requests() != 0 ||
        virtio_blk_free_descriptors() != free_baseline)
        return FATFS_LEAK_ERROR;

    printk("QS:FATFS_ITERATIONS:%d\n", QS_FATFS_ITERATIONS);
    printk("QS:FATFS_OK\n");
    m3_mark_fatfs();
    return FR_OK;
}
