#include <timeros/os.h>

#define M2C_ALLOC_DONE  (1U << 0)
#define M2C_WAIT_DONE   (1U << 1)
#define M2C_IPI_DONE    (1U << 2)
#define M2C_RFENCE_DONE (1U << 3)
#define M2C_SCHED_DONE  (1U << 4)
#define M2C_ALL_DONE    (M2C_ALLOC_DONE | M2C_WAIT_DONE | M2C_IPI_DONE | \
                         M2C_RFENCE_DONE | M2C_SCHED_DONE)
#define M3_VIRTQUEUE_DONE   (1U << 5)
#define M3_BLOCK_IRQ_DONE   (1U << 6)
#define M3_BLOCK_STRESS_DONE (1U << 7)
#define M3_FATFS_DONE       (1U << 8)
#define M3_ALL_DONE         (M2C_ALL_DONE | M3_VIRTQUEUE_DONE | \
                             M3_BLOCK_IRQ_DONE | M3_BLOCK_STRESS_DONE | \
                             M3_FATFS_DONE)
#define M4_NET_LINK_DONE   (1U << 9)
#define M4_NET_IRQ_DONE    (1U << 10)
#define M4_NET_TX_DONE     (1U << 11)
#define M4_NET_RX_DONE     (1U << 12)
#define M4_NET_RESET_DONE  (1U << 13)
#define M4_NET_STRESS_DONE (1U << 14)
#define M4_ALL_DONE        (M3_ALL_DONE | M4_NET_LINK_DONE | \
                            M4_NET_IRQ_DONE | M4_NET_TX_DONE | \
                            M4_NET_RX_DONE | M4_NET_RESET_DONE | \
                            M4_NET_STRESS_DONE)

#ifndef QS_STRESS_MIN_TICKS
#define QS_STRESS_MIN_TICKS 0ULL
#endif

static u32 completed;
static u32 finished;
static u64 started_at;

static void mark(u32 bit)
{
#ifdef QS_M2C_TEST
    __atomic_fetch_or(&completed, bit, __ATOMIC_RELEASE);
#else
    (void)bit;
#endif
}

void m2c_selftest_init(void)
{
#ifdef QS_M2C_TEST
    __atomic_store_n(&completed, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&finished, 0, __ATOMIC_RELAXED);
    started_at = r_mtime();
#endif
}

void m2c_mark_alloc(void) { mark(M2C_ALLOC_DONE); }
void m2c_mark_wait(void) { mark(M2C_WAIT_DONE); }
void m2c_mark_ipi(void) { mark(M2C_IPI_DONE); }
void m2c_mark_rfence(void) { mark(M2C_RFENCE_DONE); }
void m2c_mark_sched(void) { mark(M2C_SCHED_DONE); }

static void m3_mark(u32 bit)
{
#ifdef QS_M3_TEST
    mark(bit);
#else
    (void)bit;
#endif
}

void m3_mark_virtqueue(void) { m3_mark(M3_VIRTQUEUE_DONE); }
void m3_mark_block_irq(void) { m3_mark(M3_BLOCK_IRQ_DONE); }
void m3_mark_block_stress(void) { m3_mark(M3_BLOCK_STRESS_DONE); }
void m3_mark_fatfs(void) { m3_mark(M3_FATFS_DONE); }

static void m4_mark(u32 bit)
{
#ifdef QS_M4_TEST
    mark(bit);
#else
    (void)bit;
#endif
}

void m4_mark_net_link(void) { m4_mark(M4_NET_LINK_DONE); }
void m4_mark_net_irq(void) { m4_mark(M4_NET_IRQ_DONE); }
void m4_mark_net_tx(void) { m4_mark(M4_NET_TX_DONE); }
void m4_mark_net_rx(void) { m4_mark(M4_NET_RX_DONE); }
void m4_mark_net_reset(void) { m4_mark(M4_NET_RESET_DONE); }
void m4_mark_net_stress(void) { m4_mark(M4_NET_STRESS_DONE); }

void m2c_selftest_poll(void)
{
#ifdef QS_M2C_TEST
    u32 required = M2C_ALL_DONE;
#ifdef QS_M3_TEST
    required = M3_ALL_DONE;
#endif
#ifdef QS_M4_TEST
    required = M4_ALL_DONE;
#endif
    if ((__atomic_load_n(&completed, __ATOMIC_ACQUIRE) & required) != required)
        return;

    u64 elapsed = r_mtime() - started_at;
    if (elapsed < QS_STRESS_MIN_TICKS)
        return;
    if (__atomic_exchange_n(&finished, 1, __ATOMIC_ACQ_REL))
        return;

    printk("QS:STRESS_ELAPSED_TICKS:%d\n", (int)elapsed);
#ifdef QS_M4_STRESS
    printk("QS:TEST_PASS:m4-stress\n");
#elif defined(QS_M4_TEST)
    printk("QS:TEST_PASS:m4-smoke\n");
#elif defined(QS_M3_STRESS)
    printk("QS:TEST_PASS:m3-stress\n");
#elif defined(QS_M3_TEST)
    printk("QS:TEST_PASS:m3-smoke\n");
#elif defined(QS_M2C_STRESS)
    printk("QS:TEST_PASS:m2c-stress\n");
#else
    printk("QS:TEST_PASS:m2c-smoke\n");
#endif
    *(volatile u32 *)(uintptr_t)QEMU_TEST_BASE = QEMU_TEST_PASS;
    for (;;)
        asm volatile("wfi");
#endif
}
