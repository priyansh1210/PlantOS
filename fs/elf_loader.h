#ifndef FS_ELF_LOADER_H
#define FS_ELF_LOADER_H

#include <plantos/types.h>

/* Result of loading an ELF */
struct elf_info {
    uint64_t entry;       /* Entry point address */
    uint64_t load_base;   /* Lowest mapped virtual address (page-aligned) */
    uint64_t num_pages;   /* Total 4KB pages allocated */
};

/* Load an ELF binary from the VFS path into memory.
 * Returns 0 on success, negative on error. */
int elf_load(const char *path, struct elf_info *info);

/* Unload ELF pages: unmap and free num_pages starting at load_base */
void elf_unload(uint64_t load_base, uint64_t num_pages);

#endif
