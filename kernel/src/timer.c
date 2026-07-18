#include <timeros/os.h>

#define CLOCK_FREQ 10000000
#define TICKS_PER_SEC 100

void set_next_trigger(void)
{
    struct cpu *cpu = cpu_this();
    cpu->timer_deadline = r_mtime() + CLOCK_FREQ / TICKS_PER_SEC;
    sbi_set_timer(cpu->timer_deadline);
}

void timer_init(void)
{
    struct cpu *cpu = cpu_this();
    cpu->timer_ticks = 0;
    cpu->timer_deadline = 0;
    __atomic_store_n(&cpu->need_resched, 0, __ATOMIC_RELAXED);

    reg_t sie = r_sie();
    w_sie(sie | SIE_STIE | SIE_SSIE);
    set_next_trigger();
    intr_on();
}

void timer_tick(void)
{
    struct cpu *cpu = cpu_this();
    cpu->timer_ticks++;
    set_next_trigger();
    __atomic_store_n(&cpu->need_resched, 1, __ATOMIC_RELEASE);
}

int cpu_take_resched(void)
{
    return __atomic_exchange_n(&cpu_this()->need_resched, 0,
                               __ATOMIC_ACQ_REL);
}

uint64_t get_time_us(void)
{
    return r_mtime() / (CLOCK_FREQ / 1000000ULL);
}
