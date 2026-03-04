#ifndef PLANTOS_MULTIBOOT_H
#define PLANTOS_MULTIBOOT_H

#include <plantos/types.h>

#define MULTIBOOT_MAGIC 0x2BADB002

/* Multiboot info flags */
#define MULTIBOOT_INFO_MEMORY   0x001
#define MULTIBOOT_INFO_MMAP     0x040

struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;        /* KB of lower memory (below 1MB) */
    uint32_t mem_upper;        /* KB of upper memory (above 1MB) */
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
} PACKED;

#define MULTIBOOT_MEMORY_AVAILABLE 1

struct multiboot_mmap_entry {
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} PACKED;

#endif
