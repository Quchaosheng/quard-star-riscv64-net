#include <timeros/os.h>
#ifdef QS_M5_TEST
#include <timeros/net/net_stack.h>
#endif

void os_main(const void *fdt);

#ifndef QS_ALLOC_ITERATIONS
#define QS_ALLOC_ITERATIONS 10000
#endif
#define SMP_ALLOC_ITERATIONS QS_ALLOC_ITERATIONS
#define SMP_ALLOC_TIMEOUT_TICKS 100000000ULL

static u32 smp_alloc_start;
static u32 smp_alloc_done;
static u32 smp_alloc_failed;
static u32 smp_sched_start;

static void smp_allocator_worker()
{
   u64 hartid = cpu_this()->hartid;
   for (u64 i = 0; i < SMP_ALLOC_ITERATIONS; i++) {
      PhysPageNum ppn = kalloc();
      if (ppn.value == 0) {
         __atomic_store_n(&smp_alloc_failed, 1, __ATOMIC_RELEASE);
         break;
      }

      u64 *page = (u64 *)(uintptr_t)phys_addr_from_phys_page_num(ppn).value;
      u64 marker = 0x5153000000000000ULL ^ (hartid << 32) ^ i;
      page[0] = marker;
      page[PAGE_SIZE / sizeof(u64) - 1] = ~marker;
      if (page[0] != marker || page[PAGE_SIZE / sizeof(u64) - 1] != ~marker)
         __atomic_store_n(&smp_alloc_failed, 1, __ATOMIC_RELEASE);
      kfree(ppn);
   }
   __atomic_fetch_add(&smp_alloc_done, 1, __ATOMIC_RELEASE);
}

static void smp_allocator_test()
{
   if (cpu_count() < 2)
      return;

   u64 baseline = free_page_count();
   __atomic_store_n(&smp_alloc_failed, 0, __ATOMIC_RELAXED);
   __atomic_store_n(&smp_alloc_done, 0, __ATOMIC_RELAXED);
   __atomic_store_n(&smp_alloc_start, 1, __ATOMIC_RELEASE);
   smp_allocator_worker();

   u64 deadline = r_mtime() + SMP_ALLOC_TIMEOUT_TICKS;
   while (__atomic_load_n(&smp_alloc_done, __ATOMIC_ACQUIRE) < (u32)cpu_count()) {
      if (r_mtime() >= deadline) {
         printk("QS:TEST_FAIL:m2a-alloc:timeout\n");
         panic("SMP allocator timeout");
      }
   }

   if (__atomic_load_n(&smp_alloc_failed, __ATOMIC_ACQUIRE) ||
       free_page_count() != baseline) {
      printk("QS:TEST_FAIL:m2a-alloc:corruption\n");
      panic("SMP allocator test");
   }
   printk("QS:SMP_ALLOC_OK\n");
   printk("QS:STRESS_ALLOC_OPS:%d\n",
          SMP_ALLOC_ITERATIONS * cpu_count());
   m2c_mark_alloc();
}

static void secondary_main()
{
   kvminithart();
   set_kernel_trap_entry();
   cpu_publish_online();
   while (!__atomic_load_n(&smp_alloc_start, __ATOMIC_ACQUIRE))
      asm volatile("nop");
   smp_allocator_worker();
   while (!__atomic_load_n(&smp_sched_start, __ATOMIC_ACQUIRE))
      asm volatile("nop");
   timer_init();
   scheduler();
}

void kernel_entry(u64 hartid, const void *fdt)
{
   cpu_bind(hartid);
   if (hartid != 0)
      secondary_main();

   cpu_discover(fdt, hartid);
   os_main(fdt);
}

void os_main(const void *fdt)
{
   printk("QS:BOOT_OK\n");
   printk("QS:KERNEL_READY\n");
   // 内存分配器初始化
   frame_alloctor_init();
   //初始化内存
   kvminit();
   binit();
   //初始化磁盘
   virtio_disk_init();
#if defined(QS_M4_TEST) || defined(QS_M5_TEST)
   if (virtio_net_init() < 0)
      panic("virtio net init");
#endif
#ifdef QS_M5_TEST
   if (net_stack_init() < 0)
      panic("network stack init");
#endif
   //初始化进程
   procinit();
   //加载initproc进程
   load_app(0);
   app_init(0);
#ifdef QS_M5_TEST
   if (task_create_kernel(net_stack_worker, net_stack_default()) < 0)
      panic("network worker task");
#endif

   //映射内核
   kvminithart();
   //trap初始化
   set_kernel_trap_entry();

   cpu_publish_online();
   m2c_selftest_init();
   printk("QS:HART_ONLINE:0\n");
   cpu_start_secondaries(fdt);
   smp_allocator_test();

   get_app_names();

   //初始化时钟
   timer_init();

   if (cpu_count() > 1)
      printk("QS:TEST_PASS:m2a-smoke\n");
   else
      printk("QS:TEST_PASS:m1-smoke\n");

   __atomic_store_n(&smp_sched_start, 1, __ATOMIC_RELEASE);
   scheduler();

}
