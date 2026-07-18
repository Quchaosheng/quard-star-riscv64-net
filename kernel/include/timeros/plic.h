#ifndef TOS_PLIC_H__
#define TOS_PLIC_H__

#include <timeros/types.h>

#define PLIC_BASE 0x0c000000ULL
#define PLIC_SIZE 0x00210000ULL
#define PLIC_VIRTIO0_IRQ 1

void plic_init_hart(void);
u32 plic_claim(void);
void plic_complete(u32 irq);

#endif
