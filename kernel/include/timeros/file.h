#ifndef TOS_FILE_H
#define TOS_FILE_H

#include <stddef.h>
#include <timeros/net/net_err.h>

void file_init(void);
void file_close_owner(int pid);
int file_open(const char *name, int writable);
int file_read(int handle, void *data, size_t length);
int file_write(int handle, const void *data, size_t length);
net_err_t file_sync(int handle);
net_err_t file_close(int handle);

#endif
