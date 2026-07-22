#include <assert.h>
#include <stdint.h>
#include <string.h>

#define TOS_OS_H__
#define FF_DEFINED 80286
#include <timeros/types.h>
#include <timeros/net/net_err.h>

typedef unsigned char BYTE;
typedef unsigned int UINT;
typedef uint32_t FSIZE_t;
typedef char TCHAR;
typedef struct { int unused; } FIL;
typedef enum {
    FR_OK,
    FR_DISK_ERR,
    FR_NO_FILE = 4,
    FR_INVALID_OBJECT = 9,
} FRESULT;

#define FA_READ 0x01
#define FA_WRITE 0x02
#define FA_CREATE_ALWAYS 0x08

struct sleeplock { int locked; };
struct TaskControlBlock { int pid; };
struct TaskControlBlock *current_proc(void);
void sleeplock_init(struct sleeplock *lock);
void sleeplock_acquire(struct sleeplock *lock);
void sleeplock_release(struct sleeplock *lock);

static struct TaskControlBlock test_proc = { 1 };
static FRESULT open_result;
static FRESULT expand_result;
static int open_calls;
static int expand_calls;
static int write_calls;
static int sync_calls;
static int close_calls;

struct TaskControlBlock *current_proc(void) { return &test_proc; }
void sleeplock_init(struct sleeplock *lock) { lock->locked = 0; }
void sleeplock_acquire(struct sleeplock *lock) { assert(!lock->locked); lock->locked = 1; }
void sleeplock_release(struct sleeplock *lock) { assert(lock->locked); lock->locked = 0; }

FRESULT f_open(FIL *file, const TCHAR *path, BYTE mode)
{
    (void)file; (void)path; (void)mode;
    open_calls++;
    return open_result;
}

FRESULT f_expand(FIL *file, FSIZE_t size, BYTE opt)
{
    (void)file; (void)size; (void)opt;
    expand_calls++;
    return expand_result;
}

FRESULT f_write(FIL *file, const void *buffer, UINT btw, UINT *bw)
{
    (void)file; (void)buffer;
    write_calls++;
    *bw = btw;
    return FR_OK;
}

FRESULT f_read(FIL *file, void *buffer, UINT btr, UINT *br)
{
    (void)file; (void)buffer; (void)btr;
    *br = 0;
    return FR_OK;
}

FRESULT f_sync(FIL *file) { (void)file; sync_calls++; return FR_OK; }
FRESULT f_close(FIL *file) { (void)file; close_calls++; return FR_OK; }

#ifndef FILE_SOURCE
#define FILE_SOURCE "../../kernel/src/file.c"
#endif
#include FILE_SOURCE

static void reset_mocks(void)
{
    open_result = FR_OK;
    expand_result = FR_OK;
    open_calls = expand_calls = write_calls = sync_calls = close_calls = 0;
    test_proc.pid = 1;
    file_init();
}

static void test_open_failure_does_not_close_unopened_file(void)
{
    reset_mocks();
    open_result = FR_NO_FILE;
    assert(file_open("missing", 0) == NET_ERR_IO);
    assert(open_calls == 1);
    assert(close_calls == 0);
}

static void test_expand_failure_closes_open_file(void)
{
    reset_mocks();
    expand_result = FR_DISK_ERR;
    assert(file_open("broken", 1) == NET_ERR_IO);
    assert(open_calls == 1);
    assert(expand_calls == 1);
    assert(close_calls == 1);
}

static void test_handle_is_owned_by_opening_process(void)
{
    unsigned char byte = 7;
    reset_mocks();
    int handle = file_open("owned", 1);
    assert(handle >= 0);
    test_proc.pid = 2;
    assert(file_write(handle, &byte, 1) == NET_ERR_PARAM);
    assert(file_sync(handle) == NET_ERR_PARAM);
    assert(file_close(handle) == NET_ERR_PARAM);
    assert(write_calls == 0);
    assert(sync_calls == 0);
    assert(close_calls == 0);
    test_proc.pid = 1;
    assert(file_close(handle) == NET_ERR_OK);
    assert(close_calls == 1);
}

int main(void)
{
    test_open_failure_does_not_close_unopened_file();
    test_expand_failure_closes_open_file();
    test_handle_is_owned_by_opening_process();
    return 0;
}
