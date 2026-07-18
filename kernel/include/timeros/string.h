#ifndef TOS_STRING_H__
#define TOS_STRING_H__

#include <timeros/os.h>
size_t strlen(const char *str);
size_t strnlen(const char *str, size_t max);
void* memcpy(void *dest, const void *src, size_t count);
void *memmove(void *dest, const void *src, size_t count);
int memcmp(const void *lhs, const void *rhs, size_t count);
void *memchr(const void *ptr, int ch, size_t count);
char *strchr(const char *str, int ch);
char *strrchr(const char *str, int ch);
void* memset(void *dest, int ch, size_t count);
int strcmp(const char *lhs, const char *rhs);
void strncat(char *dest, const char *src, int n);
#endif
