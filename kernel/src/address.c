#include <timeros/os.h>

static u32 rfence_reported;

PhysAddr phys_addr_from_size_t(uint64_t v) {
    PhysAddr addr;
    addr.value = v & ((1ULL << PA_WIDTH_SV39) - 1);
    return addr;
}

/* 把物理地址转换为物理页号 */
PhysPageNum phys_page_num_from_size_t(uint64_t v) {
    PhysPageNum pageNum;
    pageNum.value = v & ((1ULL << PPN_WIDTH_SV39) - 1);
    return pageNum;
}

uint64_t size_t_from_phys_addr(PhysAddr v) {
    return v.value;
}

uint64_t size_t_from_phys_page_num(PhysPageNum v) {
    return v.value;
}
/* 从物理页号转换为实际物理地址 */
PhysAddr phys_addr_from_phys_page_num(PhysPageNum ppn)
{
    PhysAddr addr;
    addr.value = ppn.value << PAGE_SIZE_BITS ;
    return addr;
}

VirtAddr virt_addr_from_size_t(uint64_t v) {
    VirtAddr addr;
    addr.value = v & ((1ULL << VA_WIDTH_SV39) - 1);
    return addr;
}

VirtPageNum virt_page_num_from_size_t(uint64_t v) {
    VirtPageNum pageNum;
    pageNum.value = v & ((1ULL << VPN_WIDTH_SV39) - 1);
    return pageNum;
}

uint64_t size_t_from_virt_addr(VirtAddr v) {
    if (v.value >= (1ULL << (VA_WIDTH_SV39 - 1))) {
        return v.value | ~((1ULL << VA_WIDTH_SV39) - 1);
    } else {
        return v.value;
    }
}

uint64_t size_t_from_virt_page_num(VirtPageNum v) {
    return v.value;
}

/* 物理地址向下取整 */
PhysPageNum floor_phys(PhysAddr phys_addr) {
    PhysPageNum phys_page_num;
    phys_page_num.value = phys_addr.value / PAGE_SIZE;
    return phys_page_num;
}

/* 物理地址向上取整 */
PhysPageNum ceil_phys(PhysAddr phys_addr) {
    PhysPageNum phys_page_num;
    phys_page_num.value = (phys_addr.value + PAGE_SIZE - 1) / PAGE_SIZE;
    return phys_page_num;
}

/* 虚拟地址向下取整 */
VirtPageNum floor_virts(VirtAddr virt_addr) {
    VirtPageNum virt_page_num;
    virt_page_num.value = virt_addr.value / PAGE_SIZE;
    return virt_page_num;
}

/* 把虚拟地址转换为虚拟页号 */
VirtPageNum virt_page_num_from_virt_addr(VirtAddr virt_addr)
{
    VirtPageNum vpn;
    vpn.value =  virt_addr.value / PAGE_SIZE;
    return vpn;
}




/* 新建一个页表项 */
PageTableEntry PageTableEntry_new(PhysPageNum ppn, uint8_t PTEFlags) {
    PageTableEntry entry;
    entry.bits = (ppn.value << 10) | PTEFlags;
    return entry;
}

/* 判断页表项是否为空 */
PageTableEntry PageTableEntry_empty() {
    PageTableEntry entry;
    entry.bits = 0;
    return entry;
}

/* 获取下级页表的物理页号 */
PhysPageNum PageTableEntry_ppn(PageTableEntry *entry) {
    PhysPageNum ppn;
    ppn.value = (entry->bits >> 10) & ((1ul << 44) - 1);
    return ppn;
}

/* 获取页表项的标志位 */
uint8_t PageTableEntry_flags(PageTableEntry *entry) {
    return entry->bits & 0xFF;
}

/* 判断页表项是否为空 */
bool PageTableEntry_is_valid(PageTableEntry *entry) {
    uint8_t entryFlags = PageTableEntry_flags(entry);
    return (entryFlags & PTE_V) != 0;
}

uint8_t* get_bytes_array(PhysPageNum ppn)
{
    // 先从物理页号转换为物理地址
    PhysAddr addr = phys_addr_from_phys_page_num(ppn);
    return (uint8_t*) addr.value;
}

PageTableEntry* get_pte_array(PhysPageNum ppn)
{
    // 先从物理页号转换为物理地址
    PhysAddr addr = phys_addr_from_phys_page_num(ppn);
    return (PageTableEntry*) addr.value;
}


/* 内存分配器 */
struct run {
    struct run *next;
};

#define ALLOCATOR_PAGE_COUNT ((PHYSTOP - KERNBASE) / PAGE_SIZE)

typedef struct
{
    uint64_t start;
    uint64_t  end;      //空闲内存的结束物理页号
    struct spinlock lock;
    struct run *freelist;
    u64 free_pages;
    u8 free_map[ALLOCATOR_PAGE_COUNT];
}StackFrameAllocator;

void StackFrameAllocator_new(StackFrameAllocator* allocator) {
    allocator->start = 0;
    allocator->end = 0;
    allocator->freelist = 0;
    allocator->free_pages = 0;
    memset(allocator->free_map, 0, sizeof(allocator->free_map));
    spin_init(&allocator->lock);
}

void StackFrameAllocator_init(StackFrameAllocator *allocator, PhysPageNum l, PhysPageNum r) {
    allocator->start = l.value;
    allocator->end = r.value;
    for (u64 value = r.value; value > l.value;) {
        value--;
        PhysPageNum ppn = { .value = value };
        struct run *page = (struct run *)(uintptr_t)
            phys_addr_from_phys_page_num(ppn).value;
        page->next = allocator->freelist;
        allocator->freelist = page;
        allocator->free_pages++;
        allocator->free_map[value - (KERNBASE >> PAGE_SIZE_BITS)] = 1;
    }
}

PhysPageNum StackFrameAllocator_alloc(StackFrameAllocator *allocator) {
    PhysPageNum ppn = { .value = 0 };
    spin_lock(&allocator->lock);
    struct run *page = allocator->freelist;
    if (page != 0) {
        allocator->freelist = page->next;
        allocator->free_pages--;
        ppn = floor_phys(phys_addr_from_size_t((u64)(uintptr_t)page));
        allocator->free_map[ppn.value - (KERNBASE >> PAGE_SIZE_BITS)] = 0;
    }
    spin_unlock(&allocator->lock);
    if (ppn.value == 0)
        return ppn;
    /* clear allocated page memory. */
    PhysAddr addr = phys_addr_from_phys_page_num(ppn);
    memset((void *)(uintptr_t)addr.value, 0, PAGE_SIZE);
    return ppn;
}

void StackFrameAllocator_dealloc(StackFrameAllocator *allocator, PhysPageNum ppn) {
    uint64_t ppnValue = ppn.value;
    // 检查回收的页面之前一定被分配出去过
    if (ppnValue < allocator->start || ppnValue >= allocator->end) {
        printk("Frame ppn=%lx has not been allocated!\n", ppnValue);
        return;
    }
    struct run *page = (struct run *)(uintptr_t)
        phys_addr_from_phys_page_num(ppn).value;
    spin_lock(&allocator->lock);
    u64 index = ppnValue - (KERNBASE >> PAGE_SIZE_BITS);
    if (allocator->free_map[index]) {
        spin_unlock(&allocator->lock);
        return;
    }
    // 回收物理内存页号
    page->next = allocator->freelist;
    allocator->freelist = page;
    allocator->free_pages++;
    allocator->free_map[index] = 1;
    spin_unlock(&allocator->lock);
}





StackFrameAllocator FrameAllocatorImpl;
extern char kernelend[];
void frame_alloctor_init()
{
    // 初始化时 kernelend 需向上取整
    StackFrameAllocator_new(&FrameAllocatorImpl);
    StackFrameAllocator_init(&FrameAllocatorImpl, \
            ceil_phys(phys_addr_from_size_t((uint64_t)(uintptr_t)kernelend)), \
            ceil_phys(phys_addr_from_size_t(PHYSTOP)));
}




/* 拿到虚拟页号的三级索引，按照从高到低的顺序返回 */
void indexes(VirtPageNum vpn, size_t* result)
{
    size_t idx[3];
    for (int i = 2; i >= 0; i--) {
        idx[i] = vpn.value & 0x1ff;   // 1_1111_1111 = 0x1ff
        vpn.value >>= 9;
    }

    for (int i = 0; i < 3; i++) {
        result[i] = idx[i];
    }
}

/* 分配一页内存 */
PhysPageNum kalloc(void)
{
    PhysPageNum frame =  StackFrameAllocator_alloc(&FrameAllocatorImpl);
    //printk("frame:%d\n",frame.value);
    return frame;
}

/* 释放一页内存 */
void kfree(PhysPageNum ppn)
{
    StackFrameAllocator_dealloc(&FrameAllocatorImpl,ppn);
}

u64 free_page_count(void)
{
    spin_lock(&FrameAllocatorImpl.lock);
    u64 count = FrameAllocatorImpl.free_pages;
    spin_unlock(&FrameAllocatorImpl.lock);
    return count;
}

PageTableEntry* find_pte_create(PageTable* pt,VirtPageNum vpn)
{

    // 拿到虚拟页号的三级索引，保存到idx数组中
    size_t idx[3];
    indexes(vpn, idx);
    //根节点
    PhysPageNum ppn = pt->root_ppn;
    //从根节点开始遍历，如果没有pte，就分配一页内存，然后创建一个
    for (int i = 0; i < 3; i++)
    {
        //拿到具体的页表项
        PageTableEntry* pte =  &get_pte_array(ppn)[idx[i]];
            if (i == 2) {
                return pte;
            }
        //如果此项页表为空
            if (!PageTableEntry_is_valid(pte)) {
                // allocate one physical frame
                PhysPageNum frame =  kalloc();
                if (frame.value == 0) {
                    panic("find_pte_create: out of memory");
                }
               // install a valid next-level page table entry
               *pte =  PageTableEntry_new(frame,PTE_V);
            }
        ppn = PageTableEntry_ppn(pte);
    }
    return NULL;
}
PageTableEntry* find_pte(PageTable* pt, VirtPageNum vpn)
{
    // 拿到虚拟页号的三级索引，保存到idx数组中
    size_t idx[3];
    indexes(vpn, idx);
    //根节点
    PhysPageNum ppn = pt->root_ppn;
    //从根节点开始遍历，如果没有pte，就返回空
    for (int i = 0; i < 3; i++)
    {
        //拿到具体的页表项
        PageTableEntry* pte =  &get_pte_array(ppn)[idx[i]];
        //如果此项页表为空
        if (!PageTableEntry_is_valid(pte)) {
            return NULL;
        }
        if (i == 2) {
                return pte;
            }
        //取出进入下级页表的物理页号
        ppn = PageTableEntry_ppn(pte);
    }
    return NULL;
}

void PageTable_map(PageTable* pt,VirtAddr va, PhysAddr pa, u64 size ,uint8_t pteflgs)
{
    if(size == 0)
        panic("mappages: size");

    PhysPageNum ppn = floor_phys(pa);
    VirtPageNum vpn = floor_virts(va);
    u64 last = (va.value + size - 1) / PAGE_SIZE;
    for(;;)
    {
        PageTableEntry* pte = find_pte_create(pt,vpn);
        assert(!PageTableEntry_is_valid(pte));
        *pte = PageTableEntry_new(ppn,PTE_V | pteflgs);

        if( vpn.value == last )
            break;

        // 一页一页映射
        vpn.value+=1;
        ppn.value+=1;
    }
}


int uvmcopy(PageTable* old, PageTable* new, u64 sz)
{
    PageTableEntry* pte;
    u64 i;
    u8 flags;

    for (i = 0; i <= sz; i+=PAGE_SIZE)
    {
        VirtPageNum vpn = floor_virts(virt_addr_from_size_t(i));
        pte = find_pte(old,vpn);

        if (pte != 0)
        {
            /* 将PTE 转换为物理地址*/
            u64 phyaddr = PTE2PA(pte->bits);
            /* 得到PTE的映射 flags */
            flags = PTE_FLAGS(pte->bits);
            /* 分配一页内存 */
            PhysPageNum ppn = kalloc();
            if (ppn.value == 0) {
                return -1;
            }
            u64 paddr = phys_addr_from_phys_page_num(ppn).value;
            /* 拷贝内存 */
            memcpy((void*)paddr,(void*)phyaddr,PAGE_SIZE);

            /* 映射内存 */
            PageTable_map(new,virt_addr_from_size_t(i), \
                              phys_addr_from_size_t(paddr),PAGE_SIZE,flags);

        }

    }
    return 0;
}
/* 取消映射 */
void uvmunmap(PageTable* pt, VirtPageNum vpn, u64 npages, int do_free)
{
    PageTableEntry* pte;
    u64 a;
    for (a = vpn.value; a < vpn.value + npages; a++)
    {
        pte = find_pte(pt,virt_page_num_from_size_t(a));
        if(pte !=0 )
        {
            if(do_free)
            {
                u64 phyaddr = PTE2PA(pte->bits);
                PhysPageNum ppn = floor_phys(phys_addr_from_size_t(phyaddr));
                kfree(ppn);
            }
            *pte = PageTableEntry_empty();
        }
    }
}

/* unmap page-table mappings and free memory. */
void freewalk(PhysPageNum ppn)
{
    for (int i = 0; i < 512; i++)
    {
        PageTableEntry* pte =  &get_pte_array(ppn)[i];
        if((pte->bits & PTE_V) && (pte->bits & (PTE_R|PTE_W|PTE_X)) == 0)
        {
            PhysPageNum child_ppn = PageTableEntry_ppn(pte);
            freewalk(child_ppn);
            *pte = PageTableEntry_empty();
        }
        else if(pte->bits & PTE_V)
        {
            panic("freewalk: leaf");
        }
    }
    kfree(ppn);
}

void uvmfree(PageTable* pt , u64 sz)
{
    if(sz > 0)
    {
        uvmunmap(pt,floor_virts(virt_addr_from_size_t(0)),sz/PAGE_SIZE,1);
    }
    freewalk(pt->root_ppn);
}

void tlb_shootdown_all(void)
{
  u64 mask = 0;
  struct cpu *self = cpu_this();

  sfence_vma();
  for (int i = 0; i < MAX_CPUS; i++) {
    if (&cpus[i] != self && cpus[i].present && cpus[i].online)
      mask |= 1ULL << cpus[i].hartid;
  }

  if (mask != 0) {
    struct sbiret ret = sbi_remote_sfence_vma(mask, 0, 0, ~0ULL);
    if (ret.error != 0)
      panic("tlb_shootdown_all: SBI RFENCE failed");
#ifdef QS_M2C_TEST
    if (!__atomic_exchange_n(&rfence_reported, 1, __ATOMIC_ACQ_REL)) {
      printk("QS:RFENCE_OK\n");
      m2c_mark_rfence();
    }
#endif
  }
}


void proc_freepagetable(PageTable* pagetable, u64 sz)
{
  tlb_shootdown_all();
  uvmunmap(pagetable, floor_virts(virt_addr_from_size_t(TRAMPOLINE)), 1, 0);
  uvmunmap(pagetable, floor_virts(virt_addr_from_size_t(TRAPFRAME)), 1, 0);
  uvmfree(pagetable, sz);
}


extern char etext[];
extern char trampoline[];

PageTable kvmmake(void)
{
    PageTable pt;
    PhysPageNum root_ppn =  kalloc();
    if (root_ppn.value == 0) {
        panic("kvmmake: out of memory");
    }
    pt.root_ppn = root_ppn;

    // map kernel text executable and read-only.
    PageTable_map(&pt,virt_addr_from_size_t(KERNBASE),phys_addr_from_size_t(KERNBASE), \
                    (u64)etext-KERNBASE , PTE_R | PTE_X ) ;
    // map kernel data and the physical RAM we'll make use of.
    PageTable_map(&pt,virt_addr_from_size_t((u64)etext),phys_addr_from_size_t((u64)etext ), \
                    PHYSTOP - (u64)etext , PTE_R | PTE_W ) ;
    // map the trampoline for trap entry/exit to the highest virtual address in the kernel.
    PageTable_map(&pt, virt_addr_from_size_t(TRAMPOLINE), phys_addr_from_size_t((u64)trampoline), \
                    PAGE_SIZE, PTE_R | PTE_X );
    // map virtio MMIO region for runtime interrupt/data path.
    PageTable_map(&pt, virt_addr_from_size_t(VIRTIO0), phys_addr_from_size_t(VIRTIO0),
                    PAGE_SIZE, PTE_R | PTE_W );
    PageTable_map(&pt, virt_addr_from_size_t(VIRTIO1), phys_addr_from_size_t(VIRTIO1),
                    PAGE_SIZE, PTE_R | PTE_W );
    PageTable_map(&pt, virt_addr_from_size_t(PLIC_BASE),
                    phys_addr_from_size_t(PLIC_BASE), PLIC_SIZE,
                    PTE_R | PTE_W);
    PageTable_map(&pt, virt_addr_from_size_t(QEMU_TEST_BASE),
                    phys_addr_from_size_t(QEMU_TEST_BASE), PAGE_SIZE,
                    PTE_R | PTE_W);
#ifdef QS_M9_PMP_TEST
    PageTable_map(&pt, virt_addr_from_size_t(PMP_PROBE_VA),
                    phys_addr_from_size_t(PMP_TRUSTED_BASE), PAGE_SIZE,
                    PTE_R);
#endif

    //allocate and map a kernel stack for each process.
    proc_mapstacks(&pt);

    return pt;
}

PageTable kernel_pagetable;
u64 kernel_satp;
void kvminit()
{
  kernel_pagetable = kvmmake();
  kernel_satp = MAKE_SATP(kernel_pagetable.root_ppn.value);
  printk("kernel satp:%lx\n",kernel_satp);
}

void kvminithart()
{
  // wait for any previous writes to the page table memory to finish.
  sfence_vma();
  w_satp(kernel_satp);
  // flush stale entries from the TLB.
  sfence_vma();
  reg_t satp = r_satp();
}
