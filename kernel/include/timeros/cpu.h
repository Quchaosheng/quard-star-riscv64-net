#ifndef TOS_CPU_H__
#define TOS_CPU_H__

#include <timeros/types.h>

#define MAX_CPUS 7

struct cpu {
    u64 hartid;
    u32 present;
    u32 started;
    u32 online;
    int noff;
    int intena;
};

extern struct cpu cpus[MAX_CPUS];

void cpu_bind(u64 hartid);
struct cpu *cpu_this(void);
void cpu_discover(const void *fdt, u64 boot_hartid);
int cpu_count(void);
void cpu_publish_online(void);
void cpu_start_secondaries(const void *fdt);

#endif
