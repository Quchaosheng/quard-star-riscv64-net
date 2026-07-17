#include <timeros/os.h>

#define MAX_USER_STR 128
#define WRITE_CHUNK  128

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
		for (size_t i = 0; i < n; i++) {
			sbi_console_putchar(chunk[i]);
		}
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
	default:
		printk("unsupported syscall id:%d\n", syscall_id);
		return -1;
	}
}
