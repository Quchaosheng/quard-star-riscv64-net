#include <timeros/os.h>
void uart_puts(char *s)
{
	while (*s) {
		sbi_console_putchar(*s++);
	}
}


static char out_buf[1000]; // buffer for _vprintf()
static struct spinlock print_lock;

void console_write(const char *data, size_t len)
{
	spin_lock(&print_lock);
	for (size_t i = 0; i < len; i++)
		sbi_console_putchar(data[i]);
	spin_unlock(&print_lock);
}

static int _vprintf(const char* s, va_list vl)
{
	int res = _vsnprintf(NULL, -1, s, vl);
	if (res+1 >= sizeof(out_buf)) {
		uart_puts("error: output string size overflow\n");
		while(1) {}
	}
	_vsnprintf(out_buf, res + 1, s, vl);
	uart_puts(out_buf);
	return res;
}

int printk(const char* s, ...)
{
	int res = 0;
	spin_lock(&print_lock);
	va_list vl;
	va_start(vl, s);
	res = _vprintf(s, vl);
	va_end(vl);
	spin_unlock(&print_lock);
	return res;
}

void panic(char *s)
{
	printk("panic: ");
	printk(s);
	printk("\n");
	while(1){};
}
