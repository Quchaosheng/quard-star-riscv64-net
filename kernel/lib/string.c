#include <timeros/string.h>
//计算字符串的长度
size_t strlen(const char *str)
{
    char *ptr = (char *)str;
    while (*ptr != EOS)
    {
        ptr++;
    }
    return ptr - str;
}

size_t strnlen(const char *str, size_t max)
{
    size_t len = 0;
    while (len < max && str[len] != EOS) {
        len++;
    }
    return len;
}

// 从存储区 src 复制 n 个字节到存储区 dest。
void* memcpy(void *dest, const void *src, size_t count)
{
    char *ptr = dest;
    while (count--)
    {
        *ptr++ = *((char *)(src++));
    }
    return dest;
}

void *memmove(void *dest, const void *src, size_t count)
{
    unsigned char *d = dest;
    const unsigned char *s = src;

    if (d < s) {
        while (count--)
            *d++ = *s++;
    } else if (d > s) {
        d += count;
        s += count;
        while (count--)
            *--d = *--s;
    }
    return dest;
}

int memcmp(const void *lhs, const void *rhs, size_t count)
{
    const unsigned char *l = lhs;
    const unsigned char *r = rhs;

    while (count--) {
        if (*l != *r)
            return *l < *r ? -1 : 1;
        l++;
        r++;
    }
    return 0;
}

void *memchr(const void *ptr, int ch, size_t count)
{
    const unsigned char *p = ptr;
    while (count--) {
        if (*p == (unsigned char)ch)
            return (void *)p;
        p++;
    }
    return 0;
}

char *strrchr(const char *str, int ch)
{
    const char *last = 0;
    do {
        if (*str == (char)ch)
            last = str;
    } while (*str++ != EOS);
    return (char *)last;
}


//复制字符 ch（一个无符号字符）到参数 dest 所指向的字符串的前 n 个字符。
void* memset(void *dest, int ch, size_t count)
{
    char *ptr = dest;
    while (count--)
    {
        *ptr++ = ch;
    }
    return dest;
}


//把 lhs1 所指向的字符串和 rhs2 所指向的字符串进行比较
int strcmp(const char *lhs, const char *rhs)
{
    while (*lhs == *rhs && *lhs!= EOS && *rhs != EOS)
    {
        lhs++;
        rhs++;
    }
    return *lhs < *rhs ? -1 : *lhs > *rhs;
}


void strncat(char *dest, const char *src, int n) {
    while (*dest) {
        dest++;
    }
    while (n > 0 && *src) {
        *dest++ = *src++;
        n--;
    }
    *dest = '\0';
}
