#include <timeros/os.h>
#include <timeros/file.h>
#include <ff.h>

#define FILE_MAX 4
#define FILE_INDEX_BITS 2
#define FILE_INDEX_MASK (FILE_MAX - 1)
#define FILE_PATH_MAX 64

typedef struct {
    FIL file;
    u32 generation;
    int used;
    int writable;
} file_entry_t;

static file_entry_t files[FILE_MAX];
static unsigned char file_read_buffer[512] __attribute__((aligned(512)));
static unsigned char file_write_buffer[512] __attribute__((aligned(512)));
static struct sleeplock file_lock;

static int file_handle(int index)
{
    return (int)((files[index].generation << FILE_INDEX_BITS) | (u32)index);
}

static file_entry_t *file_find(int handle)
{
    if (handle < 0)
        return 0;
    int index = handle & FILE_INDEX_MASK;
    u32 generation = (u32)handle >> FILE_INDEX_BITS;
    file_entry_t *entry = &files[index];

    if (!entry->used || entry->generation != generation)
        return 0;
    return entry;
}

static int file_path(char *path, size_t capacity, const char *name)
{
    size_t length = 0;

    if (path == 0 || name == 0 || capacity < 5)
        return -1;
    while (name[length] != 0) {
        if (name[length] == '/' || name[length] == '\\' ||
            name[length] == ':' ||
            (name[length] == '.' && name[length + 1] == '.'))
            return -1;
        length++;
        if (length + 4 > capacity)
            return -1;
    }
    if (length == 0)
        return -1;
    path[0] = '0';
    path[1] = ':';
    path[2] = '/';
    memcpy(path + 3, name, length + 1);
    return 0;
}

void file_init(void)
{
    sleeplock_init(&file_lock);
    for (int index = 0; index < FILE_MAX; index++) {
        files[index].generation = 1;
        files[index].used = 0;
        files[index].writable = 0;
    }
}

int file_open(const char *name, int writable)
{
    char path[FILE_PATH_MAX];
    int index = -1;

    if (writable != 0 && writable != 1 ||
        file_path(path, sizeof(path), name) < 0)
        return NET_ERR_PARAM;
    sleeplock_acquire(&file_lock);
    for (int current = 0; current < FILE_MAX; current++) {
        if (!files[current].used) {
            index = current;
            break;
        }
    }
    if (index < 0) {
        sleeplock_release(&file_lock);
        return NET_ERR_FULL;
    }
    BYTE mode = writable ? FA_WRITE | FA_CREATE_ALWAYS : FA_READ;
    FRESULT result = f_open(&files[index].file, path, mode);
    if (result == FR_OK && writable)
        result = f_expand(&files[index].file, 1024 * 1024, 1);
    if (result != FR_OK) {
        if (result != FR_INVALID_OBJECT)
            f_close(&files[index].file);
        sleeplock_release(&file_lock);
        return NET_ERR_IO;
    }
    files[index].used = 1;
    files[index].writable = writable;
    int handle = file_handle(index);
    sleeplock_release(&file_lock);
    return handle;
}

int file_read(int handle, void *data, size_t length)
{
    UINT transferred = 0;

    if (data == 0 || length == 0 || length > 0xffffffffU)
        return NET_ERR_PARAM;
    sleeplock_acquire(&file_lock);
    file_entry_t *entry = file_find(handle);
    if (entry == 0 || entry->writable) {
        sleeplock_release(&file_lock);
        return NET_ERR_PARAM;
    }
    if (length > sizeof(file_read_buffer)) {
        sleeplock_release(&file_lock);
        return NET_ERR_SIZE;
    }
    FRESULT result = f_read(&entry->file, file_read_buffer,
                            (UINT)length, &transferred);
    if (result == FR_OK && transferred != 0)
        memcpy(data, file_read_buffer, transferred);
    sleeplock_release(&file_lock);
    return result == FR_OK ? (int)transferred : NET_ERR_IO;
}

int file_write(int handle, const void *data, size_t length)
{
    if (data == 0 || length == 0 || length > 0xffffffffU)
        return NET_ERR_PARAM;
    sleeplock_acquire(&file_lock);
    file_entry_t *entry = file_find(handle);
    if (entry == 0 || !entry->writable) {
        sleeplock_release(&file_lock);
        return NET_ERR_PARAM;
    }
    if (length > sizeof(file_write_buffer)) {
        sleeplock_release(&file_lock);
        return NET_ERR_FULL;
    }
    memcpy(file_write_buffer, data, length);
    UINT transferred = 0;
    FRESULT result = f_write(&entry->file, file_write_buffer,
                             (UINT)length, &transferred);
    sleeplock_release(&file_lock);
    if (result != FR_OK)
        return NET_ERR_IO;
    return transferred == length ? (int)transferred : NET_ERR_FULL;
}

net_err_t file_sync(int handle)
{
    sleeplock_acquire(&file_lock);
    file_entry_t *entry = file_find(handle);
    if (entry == 0 || !entry->writable) {
        sleeplock_release(&file_lock);
        return NET_ERR_PARAM;
    }
    FRESULT result = f_sync(&entry->file);
    sleeplock_release(&file_lock);
    return result == FR_OK ? NET_ERR_OK : NET_ERR_IO;
}

net_err_t file_close(int handle)
{
    sleeplock_acquire(&file_lock);
    file_entry_t *entry = file_find(handle);
    if (entry == 0) {
        sleeplock_release(&file_lock);
        return NET_ERR_PARAM;
    }
    FRESULT result = entry->writable ? f_sync(&entry->file) : FR_OK;
    FRESULT close_result = f_close(&entry->file);
    if (result == FR_OK)
        result = close_result;
    entry->used = 0;
    entry->writable = 0;
    entry->generation++;
    if (entry->generation == 0)
        entry->generation = 1;
    sleeplock_release(&file_lock);
    return result == FR_OK ? NET_ERR_OK : NET_ERR_IO;
}
