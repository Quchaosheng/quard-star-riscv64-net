#ifndef TIMEROS_SYSCALL_H
#define TIMEROS_SYSCALL_H

#include "types.h"
#include <stddef.h>

/* syscall */
reg_t __SYSCALL(size_t syscall_id, reg_t arg1, reg_t arg2, reg_t arg3);

#define __NR_read 63
#define __NR_write 64
#define __NR_sched_yield 124
#define __NR_exit 93
#define __NR_waitid 95
#define __NR_gettimeofday 169
#define __NR_clone 220
#define __NR_execve 221
#define __NR_socket 198
#define __NR_bind 200
#define __NR_connect 203
#define __NR_sendto 206
#define __NR_recvfrom 207
#define __NR_send 208
#define __NR_recv 209
#define __NR_close 57

#define NET_AF_INET 2
#define NET_SOCK_STREAM 1
#define NET_SOCK_DGRAM 2

typedef struct net_sockaddr_in {
    uint16_t family;
    /* Port is encoded in network byte order. */
    uint16_t port;
    /* IPv4 address uses the stack's network-byte representation. */
    uint32_t address;
} net_sockaddr_in;

typedef struct net_sendto_args {
    const char *data;
    size_t length;
    int flags;
    const net_sockaddr_in *address;
    size_t address_length;
} net_sendto_args;

typedef struct net_recvfrom_args {
    char *data;
    size_t length;
    int flags;
    net_sockaddr_in *address;
    size_t *address_length;
    int timeout_ms;
} net_recvfrom_args;

typedef struct net_send_args {
    const char *data;
    size_t length;
    int flags;
} net_send_args;

typedef struct net_recv_args {
    char *data;
    size_t length;
    int flags;
} net_recv_args;

uint64_t sys_write(size_t fd, const char* buf, size_t len);
uint64_t sys_yield();
uint64_t sys_gettime();
int sys_fork();
int sys_exec(char *name);
int sys_read(size_t fd ,const char* buf , size_t len);
char getchar();
int sys_exit(u64 exit_code);
int sys_waitpid();
int sys_socket(int domain, int type, int protocol);
int sys_bind(int fd, const net_sockaddr_in *address, size_t address_length);
int sys_connect(int fd, const net_sockaddr_in *address,
                size_t address_length);
int sys_send(int fd, const void *data, size_t length, int flags);
int sys_recv(int fd, void *data, size_t length, int flags);
int sys_sendto(int fd, const void *data, size_t length, int flags,
               const net_sockaddr_in *address, size_t address_length);
int sys_recvfrom(int fd, void *data, size_t length, int flags,
                 net_sockaddr_in *address, size_t *address_length,
                 int timeout_ms);
int sys_close(int fd);

#endif
