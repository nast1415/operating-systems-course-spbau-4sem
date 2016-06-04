#include "elf.h"

static void read_buffer(void *buffer, struct fs_file *file, int position, size_t struct_size)
{
    vfs_seek(file, position, FSS_SET);
    vfs_read(file, (char *) buffer, struct_size);
}

static int load_program_header(struct elf_phdr *elf_phdr, struct fs_file *file)
{
    pte_t *pml4 = (pte_t *) va(load_pml4());

    int rc = pt_populate_range(pml4, elf_phdr->p_vaddr, elf_phdr->p_vaddr + elf_phdr->p_memsz - 1); // sets PTE_WRITE | PTE_USER
    if (rc < 0)
        return rc;

    struct pt_iter iter;
    for_each_slot_in_range(pml4, elf_phdr->p_vaddr, elf_phdr->p_vaddr + elf_phdr->p_memsz - 1, iter) {
        const int level = iter.level;
        const int index = iter.idx[level];
        pte_t *pt = iter.pt[level];       

        DBG_ASSERT(level == 0);

        struct page *page = alloc_pages(0);
        pt[index] = page_paddr(page) | PTE_PRESENT | PTE_PT_FLAGS;
    }

    store_pml4(pa(pml4));

    void *content = kmem_alloc(elf_phdr->p_filesz);
    read_buffer(content, file, elf_phdr->p_offset, elf_phdr->p_filesz);
    memcpy((void *) elf_phdr->p_vaddr, content, elf_phdr->p_filesz);
    kmem_free(content);

    printf("Loading ends successfully\n");

    return 0;
}

static int create_stack(struct elf_hdr *elf_hdr)
{
    pte_t *pml4 = (pte_t *) va(load_pml4());

    const size_t USERSPACE_HIGH = 0x7fffffffffff;

    int rc = pt_populate_range(pml4, ALIGN_DOWN(USERSPACE_HIGH, PAGE_SIZE), USERSPACE_HIGH); // sets PTE_WRITE | PTE_USER
    if (rc < 0)
        return rc;

    struct pt_iter iter;
    (void) pt_iter_set(&iter, pml4, USERSPACE_HIGH);

    const int level = iter.level;
    const int index = iter.idx[level];
    pte_t *pt = iter.pt[level];
    
    DBG_ASSERT(level == 0);

    struct page *stack = alloc_pages(0);
    pt[index] = page_paddr(stack) | PTE_PRESENT | PTE_PT_FLAGS;
    store_pml4(pa(pml4));

    struct thread_regs *regs = thread_regs(current());
    regs->cs = USER_CODE;
    regs->ss = USER_DATA;
    regs->rsp = (uint64_t)((char*)(iter.addr) + PAGE_SIZE);
    regs->rsp = USERSPACE_HIGH;
    regs->rip = elf_hdr->e_entry;

    printf("Elf file loaded successfully\n");

    return 0;
}

static int load_program_headers(struct elf_hdr *elf_hdr, struct fs_file *file)
{
    for (int i = 0; i < elf_hdr->e_phnum; i++) {
        struct elf_phdr elf_phdr;
        read_buffer((char *) &elf_phdr, file, (int) (elf_hdr->e_phoff + i * elf_hdr->e_phentsize), elf_hdr->e_phentsize);
       
        if (elf_phdr.p_type == PT_LOAD) {
            int rc = load_program_header(&elf_phdr, file);
            if (rc < 0)
                return rc;
        }
    }

    return 0;
}

static int load_elf(void *path)
{
    struct fs_file file;
    if (vfs_open((char *) path, &file) < 0) {
        printf("vfs_open() return bad value!\n");
        return -1;
    }

    struct elf_hdr elf_hdr;
    read_buffer(&elf_hdr, &file, 0, sizeof(struct elf_hdr));
   
    int rc = load_program_headers(&elf_hdr, &file);
    if (rc < 0) {
        printf("It's an error %d while loading elf '%s'\n", rc, path);
        return rc;
    }
    
    rc = create_stack(&elf_hdr);
    if (rc < 0)
        return rc;

    vfs_release(&file);
    return 0;
}

pid_t run_elf(const char *path)
{
    printf("We're starting to run elf '%s'\n", path);
    return create_kthread(load_elf, (void *) path);
}

void write(struct thread_regs *regs);
void fork(struct thread_regs *regs);

void elf_syscall(struct thread_regs *regs)
{
    if (regs->rax == 0)
        write(regs);

    if (regs->rax == 1)
        fork(regs);
}

void write(struct thread_regs *regs)
{
    for (int i = 0; i < (int) regs->rcx; i++) {
        printf("%c", *((char *) regs->rbx + i));
	}
}

void fork(struct thread_regs *regs)
{
    (void) regs;
    // :(
}
