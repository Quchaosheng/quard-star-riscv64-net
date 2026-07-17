#include <string.h>

void *memcpy(void *dest, const void *src, size_t count)
{
    unsigned char *out = dest;
    const unsigned char *in = src;

    while (count-- != 0) {
        *out++ = *in++;
    }
    return dest;
}

void *memset(void *dest, int value, size_t count)
{
    unsigned char *out = dest;

    while (count-- != 0) {
        *out++ = (unsigned char)value;
    }
    return dest;
}
