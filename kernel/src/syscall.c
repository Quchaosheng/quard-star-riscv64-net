#include <timeros/os.h>

#define MAX_USER_STR 128
#define WRITE_CHUNK  128
#define SOCKET_IO_MAX 1472

#include <timeros/net/net_exec.h>
#include <timeros/net/dns.h>
#include <timeros/net/net_stack.h>
#include <timeros/net/socket.h>
#include <timeros/net/tools.h>

static int copy_from_user(void *dst, const char *src, size_t len);

#ifdef QS_M6B_TEST
static int m6b_timeout_handle = -1;
#endif

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
    SOCKET_EXEC_LISTEN,
    SOCKET_EXEC_CONNECT,
    SOCKET_EXEC_SEND,
    SOCKET_EXEC_SENDTO,
    SOCKET_EXEC_CLOSE,
};

static void socket_exec(void *arg)
{
    socket_exec_t *request = arg;

    if (request->op == SOCKET_EXEC_OPEN)
        request->result = net_socket_open(request->type);
    else if (request->op == SOCKET_EXEC_BIND)
        request->result = net_socket_bind(request->handle,
                                          net_stack_default(),
                                          &request->address,
                                          request->port);
    else if (request->op == SOCKET_EXEC_LISTEN)
        request->result = net_socket_listen(request->handle, request->length);
    else if (request->op == SOCKET_EXEC_CONNECT)
        request->result = net_socket_connect_start(request->handle,
                                                    net_stack_default(),
                                                    &request->address,
                                                    request->port);
    else if (request->op == SOCKET_EXEC_SEND)
        request->result = net_socket_send(request->handle, request->data,
                                          request->length);
    else if (request->op == SOCKET_EXEC_SENDTO)
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

static int user_range_check(const char *address, size_t length, u64 required)
{
    for (size_t i = 0; i < length; i++) {
        if (translate_user_ptr(address + i, required) == 0)
            return -1;
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
    if (domain != NET_AF_INET || protocol != 0 ||
        (type != NET_SOCK_DGRAM && type != NET_SOCK_STREAM))
        return NET_ERR_NOT_SUPPORT;
    socket_exec_t request = {
        .op = SOCKET_EXEC_OPEN,
        .type = type == NET_SOCK_DGRAM ? NET_SOCKET_UDP : NET_SOCKET_TCP,
    };
    return socket_exec_wait(&request);
}

static int __sys_connect(int handle,
                         const net_sockaddr_in *user_address,
                         size_t address_length)
{
    net_sockaddr_in address;
    if (copy_socket_address(&address, user_address, address_length) < 0)
        return NET_ERR_PARAM;
    socket_exec_t request = {
        .op = SOCKET_EXEC_CONNECT,
        .handle = handle,
        .port = x_ntohs(address.port),
    };
    request.address.q_addr = address.address;
    int result = socket_exec_wait(&request);
    return result < 0 ? result : net_socket_wait_connect(handle, 0);
}

static int __sys_send(int handle, const net_send_args *user_args)
{
    net_send_args args;
    uint8_t data[TCP_MSS];
    if (copy_from_user(&args, (const char *)user_args, sizeof(args)) < 0 ||
        args.flags != 0 || args.length == 0 || args.length > sizeof(data) ||
        copy_from_user(data, args.data, args.length) < 0)
        return NET_ERR_PARAM;
    socket_exec_t request = {
        .op = SOCKET_EXEC_SEND,
        .handle = handle,
        .data = data,
        .length = (int)args.length,
    };
    int result = socket_exec_wait(&request);
    return result < 0 ? result : (int)args.length;
}

static int __sys_recv(int handle, const net_recv_args *user_args)
{
    net_recv_args args;
    uint8_t data[TCP_MSS];
    if (copy_from_user(&args, (const char *)user_args, sizeof(args)) < 0 ||
        args.flags != 0 || args.length == 0 || args.length > sizeof(data) ||
        args.data == 0 || user_range_check(args.data, args.length, PTE_W) < 0)
        return NET_ERR_PARAM;
    int result = net_socket_recv(handle, data, (int)args.length, 0);
    if (result < 0)
        return result;
    if (copy_to_user(args.data, data, (size_t)result) < 0)
        return NET_ERR_PARAM;
    return result;
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
    request.address.q_addr = address.address;
    return socket_exec_wait(&request);
}

static int __sys_listen(int handle, int backlog)
{
    socket_exec_t request = {
        .op = SOCKET_EXEC_LISTEN,
        .handle = handle,
        .length = backlog,
    };
    return socket_exec_wait(&request);
}

static int __sys_accept(int handle, net_sockaddr_in *user_address,
                        size_t *user_address_length)
{
    net_socket_accept_t accept = {0};
    size_t address_length = 0;
    int output_address = user_address != 0 || user_address_length != 0;

    if ((user_address == 0) != (user_address_length == 0))
        return NET_ERR_PARAM;
    if (output_address) {
        if (copy_from_user(&address_length,
                           (const char *)user_address_length,
                           sizeof(address_length)) < 0 ||
            address_length < sizeof(net_sockaddr_in) ||
            user_range_check((const char *)user_address,
                             sizeof(net_sockaddr_in), PTE_W) < 0 ||
            user_range_check((const char *)user_address_length,
                             sizeof(address_length), PTE_W) < 0)
            return NET_ERR_PARAM;
    }

    int result = net_socket_accept_prepare(handle, &accept);
    if (result < 0)
        return result;
    if (output_address) {
        net_sockaddr_in address = {
            .family = NET_AF_INET,
            .port = x_htons(accept.remote_port),
            .address = accept.remote_ip.q_addr,
        };
        address_length = sizeof(address);
        if (copy_to_user((char *)user_address, &address,
                         sizeof(address)) < 0) {
            net_socket_accept_abort(&accept);
            return NET_ERR_PARAM;
        }
        if (copy_to_user((char *)user_address_length, &address_length,
                         sizeof(address_length)) < 0) {
            net_socket_accept_abort(&accept);
            return NET_ERR_PARAM;
        }
    }
    return net_socket_accept_commit(&accept);
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
        .op = SOCKET_EXEC_SENDTO,
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
    size_t address_length = 0;
    if (copy_from_user(&args, (const char *)user_args, sizeof(args)) < 0 ||
        args.flags != 0 || args.length > sizeof(data) || args.data == 0)
        return NET_ERR_PARAM;
    if (user_range_check(args.data, args.length, PTE_W) < 0)
        return NET_ERR_PARAM;
    if (args.address != 0 || args.address_length != 0) {
        if (args.address == 0 || args.address_length == 0 ||
            copy_from_user(&address_length,
                           (const char *)args.address_length,
                           sizeof(address_length)) < 0 ||
            address_length < sizeof(net_sockaddr_in) ||
            user_range_check((const char *)args.address,
                             sizeof(net_sockaddr_in), PTE_W) < 0 ||
            user_range_check((const char *)args.address_length,
                             sizeof(address_length), PTE_W) < 0)
            return NET_ERR_PARAM;
        address_length = sizeof(net_sockaddr_in);
    }
    int result = net_socket_recvfrom(handle, data, (int)args.length,
                                     &source, &source_port,
                                     args.timeout_ms);
    if (result < 0)
#ifdef QS_M6B_TEST
    {
        if (result == NET_ERR_TMO)
            m6b_timeout_handle = handle;
        return result;
    }
#else
        return result;
#endif
    if (copy_to_user(args.data, data, (size_t)result) < 0)
        return NET_ERR_PARAM;
    if (args.address != 0 || args.address_length != 0) {
        net_sockaddr_in address = {
            .family = NET_AF_INET,
            .port = x_htons(source_port),
            .address = source.q_addr,
        };
        if (copy_to_user((char *)args.address, &address, sizeof(address)) < 0 ||
            copy_to_user((char *)args.address_length, &address_length,
                         sizeof(address_length)) < 0)
            return NET_ERR_PARAM;
    }
#ifdef QS_M6B_TEST
    m6b_mark_udp();
#endif
    return result;
}

static int __sys_close(int handle)
{
    socket_exec_t request = {
        .op = SOCKET_EXEC_CLOSE,
        .handle = handle,
    };
    int result = socket_exec_wait(&request);
    int wait_result = NET_ERR_OK;
    while (result == NET_ERR_NONE) {
        int err = net_socket_wait_close(handle, 0);
        if (err < 0 && wait_result == NET_ERR_OK)
            wait_result = err;
        result = socket_exec_wait(&request);
    }
#ifdef QS_M6B_TEST
    if (result == NET_ERR_OK && m6b_timeout_handle == handle) {
        m6b_mark_udp_timeout();
        m6b_timeout_handle = -1;
    }
#endif
    return result < 0 ? result : wait_result;
}

static int __sys_dns_resolve(const char *user_name, u32 *user_address)
{
    char name[MAX_USER_STR];
    if (copy_user_cstr(name, sizeof(name), user_name) < 0 ||
        user_range_check((const char *)user_address, sizeof(u32), PTE_W) < 0)
        return NET_ERR_PARAM;

    ipaddr_t server;
    ipaddr_t address;
    if (ipaddr_from_str(&server, "192.168.100.1") < 0)
        return NET_ERR_PARAM;
    net_err_t error = dns_resolve_a(net_stack_default(), &server, 53,
                                    name, &address, 2000);
    if (error < 0 || copy_to_user((char *)user_address, &address.q_addr,
                                  sizeof(address.q_addr)) < 0)
        return error < 0 ? error : NET_ERR_PARAM;
    return NET_ERR_OK;
}

static int __sys_dns_complete(void)
{
    m7a_mark_dns_complete();
    return NET_ERR_OK;
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
	case __NR_listen:
		return __sys_listen((int)arg1, (int)arg2);
	case __NR_accept:
		return __sys_accept((int)arg1, (net_sockaddr_in *)arg2,
		                    (size_t *)arg3);
	case __NR_connect:
		return __sys_connect((int)arg1, (const net_sockaddr_in *)arg2, arg3);
	case __NR_send:
		return __sys_send((int)arg1, (const net_send_args *)arg2);
	case __NR_recv:
		return __sys_recv((int)arg1, (const net_recv_args *)arg2);
	case __NR_sendto:
		return __sys_sendto((int)arg1, (const net_sendto_args *)arg2);
	case __NR_recvfrom:
		return __sys_recvfrom((int)arg1, (const net_recvfrom_args *)arg2);
	case __NR_close:
		return __sys_close((int)arg1);
	case __NR_dns_resolve:
		return __sys_dns_resolve((const char *)arg1, (u32 *)arg2);
	case __NR_dns_complete:
		return __sys_dns_complete();
	default:
		printk("unsupported syscall id:%d\n", syscall_id);
		return -1;
	}
}
