#include <libfdt.h>
#include <timeros/os.h>

#define CPU_START_TIMEOUT_TICKS 10000000ULL

struct cpu cpus[MAX_CPUS];
static int present_cpus;

void cpu_bind(u64 hartid)
{
    if (hartid >= MAX_CPUS)
        panic("cpu_bind: hart id out of range");
    cpus[hartid].hartid = hartid;
    w_tp((reg_t)(uintptr_t)&cpus[hartid]);
}

struct cpu *cpu_this(void)
{
    return (struct cpu *)(uintptr_t)r_tp();
}

static int cpu_node_enabled(const void *fdt, int node)
{
    int len;
    const char *status = fdt_getprop(fdt, node, "status", &len);
    return status == 0 || strcmp(status, "okay") == 0;
}

void cpu_discover(const void *fdt, u64 boot_hartid)
{
    if (fdt_check_header(fdt) != 0)
        panic("cpu_discover: invalid kernel dtb");

    memset(cpus, 0, sizeof(cpus));
    present_cpus = 0;

    int cpus_node = fdt_path_offset(fdt, "/cpus");
    if (cpus_node < 0)
        panic("cpu_discover: missing cpus node");

    int node;
    fdt_for_each_subnode(node, fdt, cpus_node) {
        int len;
        const char *type = fdt_getprop(fdt, node, "device_type", &len);
        if (type == 0 || strcmp(type, "cpu") != 0 || !cpu_node_enabled(fdt, node))
            continue;

        const fdt32_t *reg = fdt_getprop(fdt, node, "reg", &len);
        if (reg == 0 || len < (int)sizeof(*reg))
            panic("cpu_discover: invalid cpu reg");

        u64 hartid = fdt32_to_cpu(*reg);
        if (hartid >= MAX_CPUS)
            panic("cpu_discover: hart id out of range");
        if (cpus[hartid].present)
            panic("cpu_discover: duplicate hart id");

        cpus[hartid].hartid = hartid;
        cpus[hartid].present = 1;
        present_cpus++;
    }

    if (present_cpus == 0 || boot_hartid >= MAX_CPUS ||
        !cpus[boot_hartid].present)
        panic("cpu_discover: boot hart missing");

    cpu_bind(boot_hartid);
}

int cpu_count(void)
{
    return present_cpus;
}

void cpu_publish_online(void)
{
    struct cpu *cpu = cpu_this();
    __atomic_store_n(&cpu->started, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&cpu->online, 1, __ATOMIC_RELEASE);
}

extern char _start[];

void cpu_start_secondaries(const void *fdt)
{
    struct cpu *self = cpu_this();

    for (int i = 0; i < MAX_CPUS; i++) {
        if (!cpus[i].present || &cpus[i] == self)
            continue;

        struct sbiret ret = sbi_hart_start(cpus[i].hartid,
                                           (u64)(uintptr_t)_start,
                                           (u64)(uintptr_t)fdt);
        if (ret.error != 0)
            panic("cpu_start_secondaries: HSM start failed");

        __atomic_store_n(&cpus[i].started, 1, __ATOMIC_RELEASE);
        u64 deadline = r_mtime() + CPU_START_TIMEOUT_TICKS;
        while (!__atomic_load_n(&cpus[i].online, __ATOMIC_ACQUIRE)) {
            if (r_mtime() >= deadline)
                panic("cpu_start_secondaries: online timeout");
        }
        printk("QS:HART_ONLINE:%d\n", cpus[i].hartid);
    }
}
