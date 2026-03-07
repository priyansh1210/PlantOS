#include "fs/elf_loader.h"
#include "include/elf.h"
#include "fs/vfs.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/heap.h"
#include "lib/string.h"
#include "lib/printf.h"

int elf_load(const char *path, struct elf_info *info, uint64_t cr3) {
    memset(info, 0, sizeof(*info));

    /* Get file size */
    struct vfs_stat st;
    if (vfs_stat(path, &st) < 0) {
        kprintf("[ELF] Cannot stat '%s'\n", path);
        return -1;
    }
    if (st.size < sizeof(Elf64_Ehdr)) {
        kprintf("[ELF] File too small\n");
        return -1;
    }

    /* Read entire file into heap buffer */
    uint8_t *buf = (uint8_t *)kmalloc(st.size);
    if (!buf) {
        kprintf("[ELF] Cannot allocate %u bytes\n", st.size);
        return -1;
    }

    int fd = vfs_open(path, 0);
    if (fd < 0) {
        kfree(buf);
        return -1;
    }
    uint32_t total_read = 0;
    while (total_read < st.size) {
        int n = vfs_read(fd, buf + total_read, st.size - total_read);
        if (n <= 0) break;
        total_read += n;
    }
    vfs_close(fd);

    if (total_read < st.size) {
        kprintf("[ELF] Short read: got %u of %u bytes\n", total_read, st.size);
        kfree(buf);
        return -1;
    }

    /* Parse ELF header */
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)buf;

    if (*(uint32_t *)ehdr->e_ident != ELF_MAGIC) {
        kprintf("[ELF] Bad magic\n");
        kfree(buf);
        return -1;
    }
    if (ehdr->e_ident[4] != ELFCLASS64 || ehdr->e_type != ET_EXEC ||
        ehdr->e_machine != EM_X86_64) {
        kprintf("[ELF] Not a valid ELF64 x86_64 executable\n");
        kfree(buf);
        return -1;
    }

    info->entry = ehdr->e_entry;

    /* Scan program headers to determine page range */
    uint64_t lowest_vaddr = ~0ULL;
    uint64_t highest_end = 0;

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        Elf64_Phdr *phdr = (Elf64_Phdr *)(buf + ehdr->e_phoff + i * ehdr->e_phentsize);

        if (phdr->p_type != PT_LOAD || phdr->p_memsz == 0)
            continue;

        uint64_t seg_start = phdr->p_vaddr & ~0xFFFULL;
        uint64_t seg_end = (phdr->p_vaddr + phdr->p_memsz + 0xFFF) & ~0xFFFULL;

        if (seg_start < lowest_vaddr) lowest_vaddr = seg_start;
        if (seg_end > highest_end) highest_end = seg_end;
    }

    if (lowest_vaddr >= highest_end) {
        kprintf("[ELF] No loadable segments\n");
        kfree(buf);
        return -1;
    }

    uint64_t total_pages = (highest_end - lowest_vaddr) / PAGE_SIZE;
    info->load_base = lowest_vaddr;
    info->num_pages = total_pages;

    /* Allocate physical pages and copy segment data */
    uint64_t *page_phys = (uint64_t *)kmalloc(total_pages * sizeof(uint64_t));
    if (!page_phys) {
        kprintf("[ELF] Cannot allocate page table\n");
        kfree(buf);
        return -1;
    }

    for (uint64_t p = 0; p < total_pages; p++) {
        void *page = pmm_alloc_page();
        if (!page) {
            kprintf("[ELF] Out of memory at page %llu\n", p);
            for (uint64_t j = 0; j < p; j++)
                pmm_free_page((void *)page_phys[j]);
            kfree(page_phys);
            kfree(buf);
            return -1;
        }
        memset(page, 0, PAGE_SIZE);
        page_phys[p] = (uint64_t)page;
    }

    /* Copy segment data into the allocated pages (via physical/identity-mapped addresses) */
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        Elf64_Phdr *phdr = (Elf64_Phdr *)(buf + ehdr->e_phoff + i * ehdr->e_phentsize);

        if (phdr->p_type != PT_LOAD || phdr->p_filesz == 0)
            continue;

        for (uint64_t off = 0; off < phdr->p_filesz; off++) {
            uint64_t vaddr = phdr->p_vaddr + off;
            uint64_t page_idx = (vaddr - lowest_vaddr) / PAGE_SIZE;
            uint64_t page_off = vaddr & 0xFFF;
            uint8_t *dst = (uint8_t *)(page_phys[page_idx] + page_off);
            *dst = buf[phdr->p_offset + off];
        }
    }

    kfree(buf);

    /* Map the pages in the TARGET address space */
    for (uint64_t p = 0; p < total_pages; p++) {
        uint64_t vaddr = lowest_vaddr + p * PAGE_SIZE;
        vmm_map_page_in(cr3, vaddr, page_phys[p], VMM_PRESENT | VMM_WRITE | VMM_USER);
    }

    kfree(page_phys);

    kprintf("[ELF] Loaded '%s': entry=0x%llx, base=0x%llx, %llu pages (cr3=0x%llx)\n",
            path, info->entry, info->load_base, info->num_pages, cr3);
    return 0;
}

void elf_unload(uint64_t load_base, uint64_t num_pages) {
    for (uint64_t i = 0; i < num_pages; i++) {
        uint64_t vaddr = load_base + i * PAGE_SIZE;
        uint64_t phys = vmm_get_physical(vaddr);
        vmm_unmap_page(vaddr);
        if (phys) {
            pmm_free_page((void *)phys);
        }
    }
}
