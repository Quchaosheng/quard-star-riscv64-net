#include <timeros/os.h>

extern u64 _num_app[];
extern char _app_names[];
static char* app_names[MAX_TASKS];

size_t get_num_app()
{
    return _num_app[0];
}

AppMetadata get_app_data(size_t app_id)
{
    AppMetadata metadata;
    size_t num_app = get_num_app();
    assert(app_id <= num_app);

    metadata.start = _num_app[app_id];
    metadata.size = _num_app[app_id + 1] - _num_app[app_id];
    metadata.id = app_id;
    return metadata;
}

AppMetadata get_app_data_by_name(const char* path)
{
    AppMetadata metadata;
    metadata.id = -1;

    int app_num = get_num_app();
    for (size_t i = 0; i < app_num; i++) {
        if (strcmp(path, app_names[i]) == 0) {
            metadata = get_app_data(i + 1);
            printk("find app:%s id:%d\n", path, metadata.id);
            return metadata;
        }
    }
    printk("app not exist!!\n");
    return metadata;
}

void get_app_names()
{
    int app_num = get_num_app();
    printk("/**** APPS ****\n");
    printk("num app:%d\n", app_num);
    for (size_t i = 0; i < app_num; i++) {
        if (i == 0) {
            app_names[0] = _app_names;
        } else {
            size_t len = strlen(app_names[i - 1]);
            app_names[i] = (char*)((u64)app_names[i - 1] + len + 1);
        }
        printk("%s\n", app_names[i]);
    }
    printk("**************/\n");
}

u8 flags_to_mmap_prot(u8 flags)
{
    return (flags & PF_R ? PTE_R : 0) |
           (flags & PF_W ? PTE_W : 0) |
           (flags & PF_X ? PTE_X : 0);
}

void elf_check(elf64_ehdr_t *ehdr)
{
    assert(*(u32 *)ehdr == ELFMAG);
    if (ehdr->e_machine != EM_RISCV || ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        panic("only riscv64 elf file is supported");
    }
}

void load_segment(elf64_ehdr_t *ehdr, struct TaskControlBlock* proc)
{
    elf64_phdr_t *phdr;
    u64 max_end = 0;

    for (size_t i = 0; i < ehdr->e_phnum; i++) {
        phdr = (elf64_phdr_t *)((u64)ehdr + ehdr->e_phoff + ehdr->e_phentsize * i);
        if (phdr->p_type != PT_LOAD) {
            continue;
        }

        u64 start_va = phdr->p_vaddr;
        u64 seg_end = start_va + phdr->p_memsz;
        if (seg_end > max_end) {
            max_end = seg_end;
        }

        u8 map_perm = PTE_U | flags_to_mmap_prot(phdr->p_flags);
        u64 map_size = PGROUNDUP(phdr->p_memsz);

        for (u64 j = 0; j < map_size; j += PAGE_SIZE) {
            PhysPageNum ppn = kalloc();
            if (ppn.value == 0) {
                panic("load_segment: out of memory");
            }
            u64 paddr = phys_addr_from_phys_page_num(ppn).value;

            if (j < phdr->p_filesz) {
                u64 copy_len = phdr->p_filesz - j;
                if (copy_len > PAGE_SIZE) {
                    copy_len = PAGE_SIZE;
                }
                memcpy((void*)paddr, (void*)((u64)ehdr + phdr->p_offset + j), copy_len);
            }

            PageTable_map(&proc->pagetable, virt_addr_from_size_t(start_va + j),
                          phys_addr_from_size_t(paddr), PAGE_SIZE, map_perm);
        }
    }

    proc->ustack = 2 * PAGE_SIZE + PGROUNDUP(max_end);
    proc->base_size = proc->ustack;
}

void load_app(size_t app_id)
{
    AppMetadata metadata = get_app_data(app_id + 1);
    elf64_ehdr_t *ehdr = (elf64_ehdr_t*)metadata.start;
    elf_check(ehdr);

    TaskControlBlock* proc = task_create_pt(app_id);
    load_segment(ehdr, proc);
    proc->entry = (u64)ehdr->e_entry;
    proc_ustack(proc);
}
