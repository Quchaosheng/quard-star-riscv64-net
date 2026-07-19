#include <timeros/os.h>

#define MAX_USER_STR 128
#define WRITE_CHUNK  128
#define SOCKET_IO_MAX 1472

#include <timeros/net/net_exec.h>
#include <timeros/net/net_stack.h>
#include <timeros/net/socket.h>

static int copy_from_user(void *dst, const char *src, size_t len);

typedef struct _socket_exec_t {
    int op;
    int handle;
    int type;
    uint16_t port;
    const uint8_t *data;
    int length;
    ipaddr_t address;
    int result;
} socket_exec_t;

enum {
    SOCKET_EXEC_OPEN,
    SOCKET_EXEC_BIND,
    SOCKET_EXEC_SEND,
    SOCKET_EXEC_CLOSE,
};

static void socket_exec(void *arg)
{
    socket_exec_t *request = arg;

    if (request->op == SOCKET_EXEC_OPEN)
        request->result = net_socket_open(request->type);
    else if (request->op == SOCKET_EXEC_BIND)
        request->result = net_socket_bind(request->handle, request->port);
    else if (request->op == SOCKET_EXEC_SEND)
        request->result = net_socket_sendto(request->handle,
                                             net_stack_default(),
                                             &request->address,
                                             request->port, request->data,
                                             request->length);
    else if (request->op == SOCKET_EXEC_CLOSE)
        request->result = net_socket_close(request->handle);
}

static int socket_exec_wait(socket_exec_t *request)
{
    net_err_t err = net_exec_submit(socket_exec, request, 0);
    return err < 0 ? err : request->result;
}

static int copy_socket_address(net_sockaddr_in *address,
                               const net_sockaddr_in *user,
                               size_t length)
{
    if (length != sizeof(*address) ||
        copy_from_user(address, (const char *)user, sizeof(*address)) < 0 ||
        address->family != NET_AF_INET || address->port == 0)
        return -1;
    return 0;
}

static char *translate_user_ptr(const char *uaddr, u64 required)
{
	if (uaddr == 0) {
		return 0;
	}
	u64 start_va = (u64)(uintptr_t)uaddr;
	if (start_va >= MAXVA) {
		return 0;
	}

	u64 user_satp = current_user_token();
	PageTable pt;
	pt.root_ppn.value = MAKE_PAGETABLE(user_satp);

	VirtPageNum vpn = floor_virts(virt_addr_from_size_t(start_va));
	PageTableEntry* pte = find_pte(&pt, vpn);
	u64 flags = PTE_V | PTE_U | required;
	if (pte == 0 || (pte->bits & flags) != flags) {
		return 0;
	}

	u64 phyaddr = PTE2PA(pte->bits);
	u64 page_offset = start_va & (PAGE_SIZE - 1);
	return (char*)(phyaddr + page_offset);
}

static int copy_from_user(void *dst, const char *src, size_t len)
{
	char *d = (char*)dst;
	for (size_t i = 0; i < len; i++) {
		char *p = translate_user_ptr(src + i, PTE_R);
		if (p == 0) {
			return -1;
		}
		d[i] = *p;
	}
	return 0;
}

static int copy_to_user(char *dst, const void *src, size_t len)
{
	const char *s = (const char*)src;
	for (size_t i = 0; i < len; i++) {
		char *p = translate_user_ptr(dst + i, PTE_W);
		if (p == 0) {
			return -1;
		}
		*p = s[i];
	}
	return 0;
}

static int copy_user_cstr(char *dst, size_t max, const char *src)
{
	if (max == 0) {
		return -1;
	}
	for (size_t i = 0; i < max - 1; i++) {
		char *p = translate_user_ptr(src + i, PTE_R);
		if (p == 0) {
			return -1;
		}
		dst[i] = *p;
		if (dst[i] == '\0') {
			return 0;
		}
	}
	dst[max - 1] = '\0';
	return 0;
}

int __sys_write(size_t fd, const char* data, size_t len)
{
	if (fd != stdout && fd != stderr) {
		return -1;
	}
	if (len == 0) {
		return 0;
	}

	char chunk[WRITE_CHUNK];
	size_t copied = 0;
	while (copied < len) {
		size_t n = len - copied;
		if (n > WRITE_CHUNK) {
			n = WRITE_CHUNK;
		}
		if (copy_from_user(chunk, data + copied, n) < 0) {
			return -1;
		}
		console_write(chunk, n);
		copied += n;
	}

	return (int)len;
}

int __sys_read(size_t fd, const char* data, size_t len)
{
	if (fd != stdin || len == 0) {
		return -1;
	}

	size_t i;
	for (i = 0; i < len; i++) {
		int c;
		while (1) {
			c = sbi_console_getchar();
			if (c != -1) {
				break;
			}
			schedule();
		}
		char ch = (char)c;
		if (copy_to_user((char*)data + i, &ch, 1) < 0) {
			return -1;
		}
	}
	return (int)i;
}

int __sys_yield()
{
	schedule();
	return 0;
}

uint64_t __sys_gettime()
{
	return get_time_us();
}

int __sys_exec(const char* name)
{
	char app_name[MAX_USER_STR];
	if (copy_user_cstr(app_name, sizeof(app_name), name) < 0) {
		return -1;
	}
	return exec(app_name);
}

void __sys_exit(u64 exit_code)
{
	exit_current_and_run_next(exit_code);
}

int __sys_wait()
{
	return wait();
}

static int __sys_socket(int domain, int type, int protocol)
{
    if (domain != NET_AF_INET || type != NET_SOCK_DGRAM || protocol != 0)
        return NET_ERR_NOT_SUPPORT;
    socket_exec_t request = {
        .op = SOCKET_EXEC_OPEN,
        .type = NET_SOCKET_UDP,
    };
    return socket_exec_wait(&request);
}

static int __sys_bind(int handle, const net_sockaddr_in *user_address,
                      size_t address_length)
{
    net_sockaddr_in address;
    if (copy_socket_address(&address, user_address, address_length) < 0)
        return NET_ERR_PARAM;
    socket_exec_t request = {
        .op = SOCKET_EXEC_BIND,
        .handle = handle,
        .port = x_ntohs(address.port),
    };
    return socket_exec_wait(&request);
}

static int __sys_sendto(int handle, const net_sendto_args *user_args)
{
    net_sendto_args args;
    net_sockaddr_in address;
    uint8_t data[SOCKET_IO_MAX];
    if (copy_from_user(&args, (const char *)user_args, sizeof(args)) < 0 ||
        args.flags != 0 || args.length > sizeof(data) ||
        copy_socket_address(&address, args.address,
                            args.address_length) < 0 ||
        copy_from_user(data, args.data, args.length) < 0)
        return NET_ERR_PARAM;
    socket_exec_t request = {
        .op = SOCKET_EXEC_SEND,
        .handle = handle,
        .port = x_ntohs(address.port),
        .data = data,
        .length = (int)args.length,
    };
    request.address.q_addr = address.address;
    int result = socket_exec_wait(&request);
    return result < 0 ? result : (int)args.length;
}

static int __sys_recvfrom(int handle, const net_recvfrom_args *user_args)
{
    net_recvfrom_args args;
    uint8_t data[SOCKET_IO_MAX];
    ipaddr_t source;
    uint16_t source_port;
    if (copy_from_user(&args, (const char *)user_args, sizeof(args)) < 0 ||
        args.flags != 0 || args.length > sizeof(data) || args.data == 0)
        return NET_ERR_PARAM;
    int result = net_socket_recvfrom(handle, data, (int)args.length,
                                     &source, &source_port,
                                     args.timeout_ms);
    if (result < 0)
        return result;
    if (copy_to_user(args.data, data, (size_t)result) < 0)
        return NET_ERR_PARAM;
    if (args.address != 0 || args.address_length != 0) {
        size_t length;
        if (args.address == 0 || args.address_length == 0 ||
            copy_from_user(&length, (const char *)args.address_length,
                           sizeof(length)) < 0 ||
            length < sizeof(net_sockaddr_in))
            return NET_ERR_PARAM;
        net_sockaddr_in address = {
            .family = NET_AF_INET,
            .port = x_htons(source_port),
            .address = source.q_addr,
        };
        length = sizeof(address);
        if (copy_to_user((char *)args.address, &address, sizeof(address)) < 0 ||
            copy_to_user((char *)args.address_length, &length,
                         sizeof(length)) < 0)
            return NET_ERR_PARAM;
    }
    return result;
}

static int __sys_close(int handle)
{
    socket_exec_t request = {
        .op = SOCKET_EXEC_CLOSE,
        .handle = handle,
    };
    return socket_exec_wait(&request);
}

reg_t __SYSCALL(size_t syscall_id, reg_t arg1, reg_t arg2, reg_t arg3)
{
	switch (syscall_id) {
	case __NR_write:
		return __sys_write(arg1, (const char*)arg2, arg3);
	case __NR_read:
		return __sys_read(arg1, (const char*)arg2, arg3);
	case __NR_sched_yield:
		return __sys_yield();
	case __NR_exit:
		__sys_exit(arg1);
		return 0;
	case __NR_gettimeofday:
		return __sys_gettime();
	case __NR_clone:
		return __sys_fork();
	case __NR_execve:
		return __sys_exec((const char*)arg2);
	case __NR_waitid:
		return __sys_wait();
	case __NR_socket:
		return __sys_socket((int)arg1, (int)arg2, (int)arg3);
	case __NR_bind:
		return __sys_bind((int)arg1, (const net_sockaddr_in *)arg2, arg3);
	case __NR_sendto:
		return __sys_sendto((int)arg1, (const net_sendto_args *)arg2);
	case __NR_recvfrom:
		return __sys_recvfrom((int)arg1, (const net_recvfrom_args *)arg2);
	case __NR_close:
		return __sys_close((int)arg1);
	default:
		printk("unsupported syscall id:%d\n", syscall_id);
		return -1;
	}
}
