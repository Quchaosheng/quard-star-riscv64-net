#include <timeros/os.h>

#define M2C_ALLOC_DONE  (1U << 0)
#define M2C_WAIT_DONE   (1U << 1)
#define M2C_IPI_DONE    (1U << 2)
#define M2C_RFENCE_DONE (1U << 3)
#define M2C_SCHED_DONE  (1U << 4)
#define M2C_ALL_DONE    (M2C_ALLOC_DONE | M2C_WAIT_DONE | M2C_IPI_DONE | \
                         M2C_RFENCE_DONE | M2C_SCHED_DONE)

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

void m2c_selftest_poll(void)
{
#ifdef QS_M2C_TEST
    if (__atomic_load_n(&completed, __ATOMIC_ACQUIRE) != M2C_ALL_DONE)
        return;

    u64 elapsed = r_mtime() - started_at;
    if (elapsed < QS_STRESS_MIN_TICKS)
        return;
    if (__atomic_exchange_n(&finished, 1, __ATOMIC_ACQ_REL))
        return;

    printk("QS:STRESS_ELAPSED_TICKS:%d\n", (int)elapsed);
#ifdef QS_M2C_STRESS
    printk("QS:TEST_PASS:m2c-stress\n");
#else
    printk("QS:TEST_PASS:m2c-smoke\n");
#endif
    *(volatile u32 *)(uintptr_t)QEMU_TEST_BASE = QEMU_TEST_PASS;
    for (;;)
        asm volatile("wfi");
#endif
}
