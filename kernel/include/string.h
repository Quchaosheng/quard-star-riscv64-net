#ifndef TOS_STRING_COMPAT_H__
#define TOS_STRING_COMPAT_H__

#include <stddef.h>

size_t strlen(const char *str);
size_t strnlen(const char *str, size_t max);
void *memcpy(void *dest, const void *src, size_t count);
void *memmove(void *dest, const void *src, size_t count);
void *memset(void *dest, int ch, size_t count);
int memcmp(const void *lhs, const void *rhs, size_t count);
void *memchr(const void *ptr, int ch, size_t count);
char *strrchr(const char *str, int ch);
int strcmp(const char *lhs, const char *rhs);

#endif
