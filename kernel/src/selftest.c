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
#define M5_NET_ARP_DONE   (1U << 15)
#define M5_NET_PING_DONE  (1U << 16)
#ifdef QS_M4_TEST
#define M5_ALL_DONE       (M4_ALL_DONE | M5_NET_ARP_DONE | M5_NET_PING_DONE)
#else
#define M5_ALL_DONE       (M3_ALL_DONE | M5_NET_ARP_DONE | M5_NET_PING_DONE)
#endif
#define M6_QUEUE_DONE     (1U << 17)
#define M6_ARP_TIMER_DONE (1U << 18)
#define M6_LOOP_DONE      (1U << 19)
#define M6A_ALL_DONE      (M5_ALL_DONE | M6_QUEUE_DONE | \
                           M6_ARP_TIMER_DONE | M6_LOOP_DONE)
#define M6B_UDP_DONE         (1U << 20)
#define M6B_UDP_TIMEOUT_DONE (1U << 21)
#define M6B_ALL_DONE         (M6A_ALL_DONE | M6B_UDP_DONE | \
                              M6B_UDP_TIMEOUT_DONE)
#define M6C1_TCP_DONE        (1U << 22)
#define M6C1_TCP_RETRANS_DONE (1U << 23)
#define M6C1_TCP_CLOSE_DONE  (1U << 24)
#define M6C1_TCP_CLOSE_PRINTING (1U << 25)
#define M6C1_ALL_DONE        (M6B_ALL_DONE | M6C1_TCP_DONE | \
                              M6C1_TCP_RETRANS_DONE | M6C1_TCP_CLOSE_DONE)
#define M6C2_LISTEN_DONE         (1U << 26)
#define M6C2_ACCEPT_DONE         (1U << 27)
#define M6C2_ECHO_DONE           (1U << 28)
#define M6C2_CHILD_CLOSE_DONE    (1U << 29)
#define M6C2_LISTENER_CLOSE_DONE (1U << 30)
#define M6C2_CLOSE_DONE          (1U << 31)
#define M6C2_ALL_DONE (M6C1_ALL_DONE | M6C2_LISTEN_DONE | \
                       M6C2_ACCEPT_DONE | M6C2_ECHO_DONE | \
                       M6C2_CHILD_CLOSE_DONE | \
                       M6C2_LISTENER_CLOSE_DONE | M6C2_CLOSE_DONE)
#define M6C2_STRESS_CONNECTIONS 108U
#define M6C2_STRESS_PARALLEL 8U

#ifndef QS_STRESS_MIN_TICKS
#define QS_STRESS_MIN_TICKS 0ULL
#endif

static u32 completed;
static u32 finished;
static u32 m6c2_echo_claimed;
static u32 m6c2_close_claimed;
#ifdef QS_M6C2_STRESS
static u32 m6c2_stress_accepted;
static u32 m6c2_stress_echoed;
static u32 m6c2_stress_live;
static u32 m6c2_stress_peak;
static u32 m6c2_stress_released;
#endif
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
    __atomic_store_n(&m6c2_echo_claimed, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&m6c2_close_claimed, 0, __ATOMIC_RELAXED);
#ifdef QS_M6C2_STRESS
    __atomic_store_n(&m6c2_stress_accepted, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&m6c2_stress_echoed, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&m6c2_stress_live, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&m6c2_stress_peak, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&m6c2_stress_released, 0, __ATOMIC_RELAXED);
#endif
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

static void m5_mark(u32 bit)
{
#ifdef QS_M5_TEST
    mark(bit);
#else
    (void)bit;
#endif
}

void m5_mark_net_arp(void) { m5_mark(M5_NET_ARP_DONE); }
void m5_mark_net_ping(void) { m5_mark(M5_NET_PING_DONE); }

static void m6_mark(u32 bit)
{
#ifdef QS_M6A_TEST
    mark(bit);
#else
    (void)bit;
#endif
}

void m6_mark_queue(void) { m6_mark(M6_QUEUE_DONE); }
void m6_mark_arp_timer(void) { m6_mark(M6_ARP_TIMER_DONE); }
void m6_mark_loop(void) { m6_mark(M6_LOOP_DONE); }

void m6b_mark_udp(void) { mark(M6B_UDP_DONE); }
void m6b_mark_udp_timeout(void) { mark(M6B_UDP_TIMEOUT_DONE); }

static void m6c1_mark(u32 bit)
{
#ifdef QS_M6C1_TEST
    __atomic_fetch_or(&completed, bit, __ATOMIC_RELEASE);
#else
    (void)bit;
#endif
}

void m6c1_mark_tcp(void)
{
    m6c1_mark(M6C1_TCP_DONE);
}

void m6c1_mark_tcp_retrans(void)
{
    m6c1_mark(M6C1_TCP_RETRANS_DONE);
}

void m6c1_mark_tcp_close(void)
{
#ifdef QS_M6C1_TEST
    u32 old = __atomic_fetch_or(&completed, M6C1_TCP_CLOSE_PRINTING,
                                __ATOMIC_ACQ_REL);
    if ((old & M6C1_TCP_CLOSE_PRINTING) != 0)
        return;
    printk("QS:M6C1_TCP_CLOSE_OK\n");
    __atomic_fetch_or(&completed, M6C1_TCP_CLOSE_DONE, __ATOMIC_RELEASE);
#endif
}

static void m6c2_mark(u32 bit)
{
#ifdef QS_M6C2_TEST
    __atomic_fetch_or(&completed, bit, __ATOMIC_RELEASE);
#else
    (void)bit;
#endif
}

void m6c2_mark_tcp_listen(void)
{
    m6c2_mark(M6C2_LISTEN_DONE);
}

void m6c2_mark_tcp_accept(void)
{
    m6c2_mark(M6C2_ACCEPT_DONE);
#ifdef QS_M6C2_STRESS
    u32 live = __atomic_add_fetch(&m6c2_stress_live, 1,
                                  __ATOMIC_ACQ_REL);
    __atomic_add_fetch(&m6c2_stress_accepted, 1, __ATOMIC_RELAXED);
    u32 peak = __atomic_load_n(&m6c2_stress_peak, __ATOMIC_RELAXED);
    while (peak < live && !__atomic_compare_exchange_n(
               &m6c2_stress_peak, &peak, live, 0,
               __ATOMIC_RELEASE, __ATOMIC_RELAXED)) { }
#endif
}

void m6c2_mark_tcp_echo_complete(void)
{
#ifdef QS_M6C2_STRESS
    __atomic_add_fetch(&m6c2_stress_echoed, 1, __ATOMIC_RELAXED);
#endif
}

void m6c2_mark_tcp_echo(void)
{
#ifdef QS_M6C2_TEST
    if (__atomic_exchange_n(&m6c2_echo_claimed, 1, __ATOMIC_ACQ_REL))
        return;
    printk("QS:M6C2_ECHO_OK\n");
    __atomic_fetch_or(&completed, M6C2_ECHO_DONE, __ATOMIC_RELEASE);
#endif
}

void m6c2_mark_tcp_child_close(void)
{
    m6c2_mark(M6C2_CHILD_CLOSE_DONE);
#ifdef QS_M6C2_STRESS
    __atomic_sub_fetch(&m6c2_stress_live, 1, __ATOMIC_ACQ_REL);
    __atomic_add_fetch(&m6c2_stress_released, 1, __ATOMIC_RELAXED);
#endif
}

void m6c2_mark_tcp_listener_close(void)
{
    m6c2_mark(M6C2_LISTENER_CLOSE_DONE);
}

static void m6c2_publish_close(void)
{
#ifdef QS_M6C2_TEST
    u32 required = M6C2_CHILD_CLOSE_DONE | M6C2_LISTENER_CLOSE_DONE;
    u32 value = __atomic_load_n(&completed, __ATOMIC_ACQUIRE);

    if ((value & required) != required ||
        __atomic_exchange_n(&m6c2_close_claimed, 1, __ATOMIC_ACQ_REL))
        return;
    printk("QS:M6C2_CLOSE_OK\n");
    __atomic_fetch_or(&completed, M6C2_CLOSE_DONE, __ATOMIC_RELEASE);
#endif
}

static int m6c2_stress_ready(void)
{
#ifdef QS_M6C2_STRESS
    return __atomic_load_n(&m6c2_stress_accepted, __ATOMIC_ACQUIRE) ==
               M6C2_STRESS_CONNECTIONS &&
           __atomic_load_n(&m6c2_stress_echoed, __ATOMIC_ACQUIRE) ==
               M6C2_STRESS_CONNECTIONS &&
           __atomic_load_n(&m6c2_stress_released, __ATOMIC_ACQUIRE) ==
               M6C2_STRESS_CONNECTIONS &&
           __atomic_load_n(&m6c2_stress_live, __ATOMIC_ACQUIRE) == 0 &&
           __atomic_load_n(&m6c2_stress_peak, __ATOMIC_ACQUIRE) >=
               M6C2_STRESS_PARALLEL;
#else
    return 1;
#endif
}

void m2c_selftest_poll(void)
{
#ifdef QS_M2C_TEST
    m6c2_publish_close();
    u32 required = M2C_ALL_DONE;
#ifdef QS_M3_TEST
    required = M3_ALL_DONE;
#endif
#ifdef QS_M4_TEST
    required = M4_ALL_DONE;
#endif
#ifdef QS_M5_TEST
    required = M5_ALL_DONE;
#endif
#ifdef QS_M6A_TEST
    required = M6A_ALL_DONE;
#endif
#ifdef QS_M6B_TEST
    required = M6B_ALL_DONE;
#endif
#ifdef QS_M6C1_TEST
    required = M6C1_ALL_DONE;
#endif
#ifdef QS_M6C2_TEST
    required = M6C2_ALL_DONE;
#endif
    if ((__atomic_load_n(&completed, __ATOMIC_ACQUIRE) & required) != required)
        return;
    if (!m6c2_stress_ready())
        return;

    u64 elapsed = r_mtime() - started_at;
    if (elapsed < QS_STRESS_MIN_TICKS)
        return;
    if (__atomic_exchange_n(&finished, 1, __ATOMIC_ACQ_REL))
        return;

    printk("QS:STRESS_ELAPSED_TICKS:%ld\n", (long)elapsed);
#ifdef QS_M6B_TEST
#ifdef QS_M6C2_STRESS
    printk("QS:M6C2_STRESS_PARALLEL_OK\n");
    printk("QS:M6C2_STRESS_RECONNECT_OK\n");
    printk("QS:TEST_PASS:m6c2-stress\n");
#elif defined(QS_M6C2_TEST)
    printk("QS:TEST_PASS:m6c2-smoke\n");
#elif defined(QS_M6C1_TEST)
    printk("QS:TEST_PASS:m6c1-smoke\n");
#else
    printk("QS:TEST_PASS:m6b-smoke\n");
#endif
#elif defined(QS_M6A_TEST)
    printk("QS:TEST_PASS:m6a-smoke\n");
#elif defined(QS_M4_STRESS)
    printk("QS:TEST_PASS:m4-stress\n");
#elif defined(QS_M5_TEST)
    printk("QS:TEST_PASS:m5-smoke\n");
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
