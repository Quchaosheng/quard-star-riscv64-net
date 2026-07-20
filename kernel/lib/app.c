#include <timeros/os.h>
uint64_t syscall(size_t id, reg_t arg1, reg_t arg2, reg_t arg3) {

    register uintptr_t a0 asm ("a0") = (uintptr_t)(arg1);
    register uintptr_t a1 asm ("a1") = (uintptr_t)(arg2);
    register uintptr_t a2 asm ("a2") = (uintptr_t)(arg3);
    register uintptr_t a7 asm ("a7") = (uintptr_t)(id);

    asm volatile ("ecall"
		      : "+r" (a0)
		      : "r" (a1), "r" (a2), "r" (a7)
		      : "memory");
    return a0;
}

uint64_t sys_write(size_t fd, const char* buf, size_t len)
{
    return syscall(__NR_write, fd, (reg_t)(uintptr_t)buf, len);
}

uint64_t sys_yield()
{
    return syscall(__NR_sched_yield,0,0,0);
}

uint64_t sys_gettime()
{
    return syscall(__NR_gettimeofday,0,0,0);
}

int sys_read(size_t fd ,const char* buf , size_t len)
{
    return syscall(__NR_read, fd, (reg_t)(uintptr_t)buf, len);
}

int sys_fork()
{
    return syscall(__NR_clone,0,0,0);
}

int sys_exec(char* name)
{
    return syscall(__NR_execve, 0, (reg_t)(uintptr_t)name, 0);
}

int sys_waitpid(){
    return syscall(__NR_waitid,0,0,0);
}

/* 获取一个字符 */
char getchar()
{
    char data[1];
    sys_read(stdin,data,1);
    return data[0];
}

int sys_exit(u64 exit_code){
    return syscall(__NR_exit,exit_code,0,0);
}

int sys_socket(int domain, int type, int protocol)
{
    return syscall(__NR_socket, domain, type, protocol);
}

int sys_bind(int fd, const net_sockaddr_in *address, size_t address_length)
{
    return syscall(__NR_bind, fd, (reg_t)(uintptr_t)address, address_length);
}

int sys_listen(int fd, int backlog)
{
    return syscall(__NR_listen, fd, backlog, 0);
}

int sys_accept(int fd, net_sockaddr_in *address, size_t *address_length)
{
    return syscall(__NR_accept, fd, (reg_t)(uintptr_t)address,
                   (reg_t)(uintptr_t)address_length);
}

int sys_connect(int fd, const net_sockaddr_in *address,
                size_t address_length)
{
    return syscall(__NR_connect, fd, (reg_t)(uintptr_t)address,
                   address_length);
}

int sys_send(int fd, const void *data, size_t length, int flags)
{
    net_send_args args = { data, length, flags };
    return syscall(__NR_send, fd, (reg_t)(uintptr_t)&args, 0);
}

int sys_recv(int fd, void *data, size_t length, int flags)
{
    net_recv_args args = { data, length, flags };
    return syscall(__NR_recv, fd, (reg_t)(uintptr_t)&args, 0);
}

int sys_sendto(int fd, const void *data, size_t length, int flags,
               const net_sockaddr_in *address, size_t address_length)
{
    net_sendto_args args = { data, length, flags, address, address_length };
    return syscall(__NR_sendto, fd, (reg_t)(uintptr_t)&args, 0);
}

int sys_recvfrom(int fd, void *data, size_t length, int flags,
                 net_sockaddr_in *address, size_t *address_length,
                 int timeout_ms)
{
    net_recvfrom_args args = {
        data, length, flags, address, address_length, timeout_ms,
    };
    return syscall(__NR_recvfrom, fd, (reg_t)(uintptr_t)&args, 0);
}

int sys_close(int fd)
{
    return syscall(__NR_close, fd, 0, 0);
}
