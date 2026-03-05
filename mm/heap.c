#include "mm/heap.h"
#include "mm/pmm.h"
#include "lib/string.h"
#include "lib/printf.h"

#define HEAP_MAGIC 0xDEADBEEF
#define HEAP_INITIAL_PAGES 256  /* 1MB initial heap */

struct heap_block {
    uint32_t magic;
    uint32_t is_free;
    uint64_t size;          /* Size of data area (not including header) */
    struct heap_block *next;
    struct heap_block *prev;
};

static struct heap_block *heap_start = NULL;
static uint64_t heap_total_size = 0;
static uint64_t heap_used = 0;
static uint64_t heap_alloc_count = 0;

void heap_init(void) {
    /* Allocate contiguous physical pages for heap */
    /* Place heap after PMM bitmap — find a suitable region */
    extern uint8_t _kernel_end;
    uint64_t bitmap_end = (uint64_t)&_kernel_end;
    /* Skip past PMM bitmap (estimate: total_pages / 8 bytes, rounded up to page) */
    uint64_t total_pages = pmm_get_total_pages();
    uint64_t bitmap_bytes = (total_pages + 7) / 8;
    bitmap_end = (bitmap_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1); /* align kernel end */
    bitmap_end += (bitmap_bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1); /* add bitmap size */

    heap_start = (struct heap_block *)bitmap_end;
    heap_total_size = HEAP_INITIAL_PAGES * PAGE_SIZE;

    /* Mark heap pages as used in PMM so they don't get allocated */
    for (uint64_t i = 0; i < HEAP_INITIAL_PAGES; i++) {
        uint64_t page_addr = bitmap_end + i * PAGE_SIZE;
        pmm_mark_page_used(page_addr);
    }

    /* Initialize first free block spanning entire heap */
    heap_start->magic   = HEAP_MAGIC;
    heap_start->is_free = 1;
    heap_start->size    = heap_total_size - sizeof(struct heap_block);
    heap_start->next    = NULL;
    heap_start->prev    = NULL;

    heap_used = 0;
    heap_alloc_count = 0;

    kprintf("[HEAP] Initialized at 0x%llx, size: %llu KB\n",
            (uint64_t)heap_start, heap_total_size / 1024);
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;

    /* Align size to 16 bytes */
    size = (size + 15) & ~15ULL;

    struct heap_block *block = heap_start;
    while (block) {
        if (block->magic != HEAP_MAGIC) {
            kprintf("[HEAP] Corruption detected at 0x%llx!\n", (uint64_t)block);
            return NULL;
        }
        if (block->is_free && block->size >= size) {
            /* Split block if there's enough room */
            if (block->size >= size + sizeof(struct heap_block) + 16) {
                struct heap_block *new_block = (struct heap_block *)((uint8_t *)(block + 1) + size);
                new_block->magic   = HEAP_MAGIC;
                new_block->is_free = 1;
                new_block->size    = block->size - size - sizeof(struct heap_block);
                new_block->next    = block->next;
                new_block->prev    = block;
                if (block->next)
                    block->next->prev = new_block;
                block->next = new_block;
                block->size = size;
            }
            block->is_free = 0;
            heap_used += block->size;
            heap_alloc_count++;
            return (void *)(block + 1);
        }
        block = block->next;
    }

    kprintf("[HEAP] Out of memory! Requested %llu bytes\n", size);
    return NULL;
}

void kfree(void *ptr) {
    if (!ptr) return;

    struct heap_block *block = (struct heap_block *)ptr - 1;
    if (block->magic != HEAP_MAGIC) {
        kprintf("[HEAP] kfree: invalid pointer 0x%llx\n", (uint64_t)ptr);
        return;
    }
    if (block->is_free) {
        kprintf("[HEAP] kfree: double free at 0x%llx\n", (uint64_t)ptr);
        return;
    }

    block->is_free = 1;
    heap_used -= block->size;
    heap_alloc_count--;

    /* Coalesce with next block */
    if (block->next && block->next->is_free) {
        block->size += sizeof(struct heap_block) + block->next->size;
        block->next = block->next->next;
        if (block->next)
            block->next->prev = block;
    }

    /* Coalesce with previous block */
    if (block->prev && block->prev->is_free) {
        block->prev->size += sizeof(struct heap_block) + block->size;
        block->prev->next = block->next;
        if (block->next)
            block->next->prev = block->prev;
    }
}

void heap_dump_stats(void) {
    kprintf("Heap: total=%llu KB, used=%llu KB, free=%llu KB, allocs=%llu\n",
            heap_total_size / 1024,
            heap_used / 1024,
            (heap_total_size - heap_used) / 1024,
            heap_alloc_count);
}
