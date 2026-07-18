#ifndef TOS_MEMORY_H
#define TOS_MEMORY_H

#define QEMU_TEST_BASE 0x00100000ULL
#define QEMU_TEST_PASS 0x5555
#define QEMU_TEST_FAIL 0x3333

typedef enum
{
    Identical,
    Framed,
}MapType;

#endif
