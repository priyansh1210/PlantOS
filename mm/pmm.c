#include "mm/pmm.h"
#include "lib/string.h"
#include "lib/printf.h"
#include "cpu/spinlock.h"
#include <plantos/multiboot.h>

static spinlock_t pmm_lock = SPINLOCK_INIT;

extern uint8_t _kernel_end;

static uint8_t *bitmap = NULL;
static uint64_t bitmap_size = 0;    /* in bytes */
static uint64_t total_pages = 0;
static uint64_t used_pages = 0;

static inline void bitmap_set(uint64_t page) {
    bitmap[page / 8] |= (1 << (page % 8));
}

static inline void bitmap_clear(uint64_t page) {
    bitmap[page / 8] &= ~(1 << (page % 8));
}

static inline bool bitmap_test(uint64_t page) {
    return bitmap[page / 8] & (1 << (page % 8));
}

void pmm_init(uint64_t multiboot_info_addr) {
    struct multiboot_info *mb_info = (struct multiboot_info *)(uintptr_t)multiboot_info_addr;
    uint64_t max_addr = 0;

    /* Try memory map first */
    if (mb_info->flags & MULTIBOOT_INFO_MMAP) {
        struct multiboot_mmap_entry *entry = (struct multiboot_mmap_entry *)(uintptr_t)mb_info->mmap_addr;
        uint64_t mmap_end = mb_info->mmap_addr + mb_info->mmap_length;

        while ((uint64_t)(uintptr_t)entry < mmap_end) {
            if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
                uint64_t end = entry->addr + entry->len;
                if (end > max_addr)
                    max_addr = end;
            }
            entry = (struct multiboot_mmap_entry *)((uint8_t *)entry + entry->size + 4);
        }
    }

    /* Fallback to basic memory info */
    if (max_addr == 0 && (mb_info->flags & MULTIBOOT_INFO_MEMORY)) {
        max_addr = (uint64_t)(mb_info->mem_upper + 1024) * 1024;  /* mem_upper is KB above 1MB */
        kprintf("[PMM] Using basic memory info: %llu MB\n", max_addr / (1024 * 1024));
    }

    if (max_addr == 0) {
        kprintf("[PMM] WARNING: No memory info found, assuming 128MB\n");
        max_addr = 128 * 1024 * 1024;
    }

    total_pages = max_addr / PAGE_SIZE;
    bitmap_size = (total_pages + 7) / 8;

    /* Place bitmap right after kernel, page-aligned */
    bitmap = &_kernel_end;
    bitmap = (uint8_t *)(((uint64_t)bitmap + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));

    /* Mark all pages as used initially */
    memset(bitmap, 0xFF, bitmap_size);
    used_pages = total_pages;

    /* Mark available regions as free using memory map */
    if (mb_info->flags & MULTIBOOT_INFO_MMAP) {
        struct multiboot_mmap_entry *entry = (struct multiboot_mmap_entry *)(uintptr_t)mb_info->mmap_addr;
        uint64_t mmap_end = mb_info->mmap_addr + mb_info->mmap_length;

        while ((uint64_t)(uintptr_t)entry < mmap_end) {
            if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
                uint64_t start = (entry->addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
                uint64_t end = entry->addr + entry->len;
                for (uint64_t addr = start; addr + PAGE_SIZE <= end; addr += PAGE_SIZE) {
                    uint64_t page = addr / PAGE_SIZE;
                    if (page < total_pages && bitmap_test(page)) {
                        bitmap_clear(page);
                        used_pages--;
                    }
                }
            }
            entry = (struct multiboot_mmap_entry *)((uint8_t *)entry + entry->size + 4);
        }
    } else {
        /* No mmap — mark everything above 1MB as free */
        for (uint64_t i = 256; i < total_pages; i++) {
            bitmap_clear(i);
            used_pages--;
        }
    }

    /* Protect first 1MB (BIOS, VGA, etc.) */
    for (uint64_t i = 0; i < 256; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            used_pages++;
        }
    }

    /* Protect kernel + bitmap region */
    uint64_t protected_end = (uint64_t)(bitmap + bitmap_size);
    protected_end = (protected_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint64_t kernel_start_page = 0x100000 / PAGE_SIZE;
    uint64_t protected_end_page = protected_end / PAGE_SIZE;

    for (uint64_t i = kernel_start_page; i < protected_end_page && i < total_pages; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            used_pages++;
        }
    }

    kprintf("[PMM] Total: %llu MB, Free: %llu pages, Used: %llu pages\n",
            (total_pages * PAGE_SIZE) / (1024 * 1024),
            total_pages - used_pages, used_pages);
}

void *pmm_alloc_page(void) {
    spin_lock(&pmm_lock);
    for (uint64_t i = 0; i < total_pages; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            used_pages++;
            spin_unlock(&pmm_lock);
            return (void *)(i * PAGE_SIZE);
        }
    }
    spin_unlock(&pmm_lock);
    return NULL;
}

void pmm_mark_page_used(uint64_t addr) {
    uint64_t page = addr / PAGE_SIZE;
    if (page < total_pages && !bitmap_test(page)) {
        bitmap_set(page);
        used_pages++;
    }
}

void pmm_free_page(void *addr) {
    spin_lock(&pmm_lock);
    uint64_t page = (uint64_t)addr / PAGE_SIZE;
    if (page < total_pages && bitmap_test(page)) {
        bitmap_clear(page);
        used_pages--;
    }
    spin_unlock(&pmm_lock);
}

uint64_t pmm_get_total_pages(void) { return total_pages; }
uint64_t pmm_get_used_pages(void)  { return used_pages; }
uint64_t pmm_get_free_pages(void)  { return total_pages - used_pages; }
