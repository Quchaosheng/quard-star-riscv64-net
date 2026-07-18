#include <timeros/os.h>

#define PLIC_PRIORITY(irq) \
    ((volatile u32 *)(uintptr_t)(PLIC_BASE + 0x04 + 4 * (irq)))
#define PLIC_ENABLE(context) \
    ((volatile u32 *)(uintptr_t)(PLIC_BASE + 0x2000 + 0x80 * (context)))
#define PLIC_THRESHOLD(context) \
    ((volatile u32 *)(uintptr_t)(PLIC_BASE + 0x200000 + 0x1000 * (context)))
#define PLIC_CLAIM(context) \
    ((volatile u32 *)(uintptr_t)(PLIC_BASE + 0x200004 + 0x1000 * (context)))

static u64 supervisor_context(void)
{
    return 2 * cpu_this()->hartid + 1;
}

void plic_init_hart(void)
{
    u64 context = supervisor_context();
    *PLIC_PRIORITY(PLIC_VIRTIO0_IRQ) = 1;
    *PLIC_ENABLE(context) = 1U << PLIC_VIRTIO0_IRQ;
    *PLIC_THRESHOLD(context) = 0;
}

u32 plic_claim(void)
{
    return *PLIC_CLAIM(supervisor_context());
}

void plic_complete(u32 irq)
{
    *PLIC_CLAIM(supervisor_context()) = irq;
}
