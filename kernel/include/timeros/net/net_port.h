#ifndef TOS_NET_PORT_H__
#define TOS_NET_PORT_H__

#include <timeros/types.h>
#ifdef __riscv
#include <timeros/string.h>
#include <timeros/spinlock.h>
#else
#include <assert.h>
#include <string.h>
#include <stdlib.h>
struct spinlock {
    u32 locked;
    void *owner;
};
void spin_init(struct spinlock *lock);
void spin_lock(struct spinlock *lock);
void spin_unlock(struct spinlock *lock);
#endif

#define plat_memset memset
#define plat_memcpy memcpy
#define plat_memcmp memcmp
#define plat_strlen strlen

#endif
