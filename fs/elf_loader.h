#ifndef FS_ELF_LOADER_H
#define FS_ELF_LOADER_H

#include <plantos/types.h>

/* Result of loading an ELF */
struct elf_info {
    uint64_t entry;       /* Entry point address */
    uint64_t load_base;   /* Lowest mapped virtual address (page-aligned) */
    uint64_t num_pages;   /* Total 4KB pages allocated */
};

/* Load an ELF binary from the VFS path into a specific address space.
 * cr3 is the PML4 physical address of the target process.
 * Returns 0 on success, negative on error. */
int elf_load(const char *path, struct elf_info *info, uint64_t cr3);

/* Unload ELF pages: unmap and free num_pages starting at load_base.
 * Operates on the CURRENT address space (CR3). */
void elf_unload(uint64_t load_base, uint64_t num_pages);

#endif
