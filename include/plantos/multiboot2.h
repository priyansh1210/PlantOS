#ifndef PLANTOS_MULTIBOOT2_H
#define PLANTOS_MULTIBOOT2_H

#include <plantos/types.h>

#define MULTIBOOT2_MAGIC          0x36D76289
#define MULTIBOOT2_TAG_TYPE_END   0
#define MULTIBOOT2_TAG_TYPE_MMAP  6

struct multiboot2_info {
    uint32_t total_size;
    uint32_t reserved;
} PACKED;

struct multiboot2_tag {
    uint32_t type;
    uint32_t size;
} PACKED;

#define MULTIBOOT2_MEMORY_AVAILABLE 1

struct multiboot2_mmap_entry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t reserved;
} PACKED;

struct multiboot2_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    struct multiboot2_mmap_entry entries[];
} PACKED;

#endif
