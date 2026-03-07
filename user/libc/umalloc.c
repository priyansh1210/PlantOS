#include "user/libc/ulibc.h"

#define BLOCK_MAGIC 0xDEADBEEF
#define ALIGN16(x) (((x) + 15) & ~(size_t)15)

struct block {
    uint32_t magic;
    uint32_t size;      /* Data area size (not including header) */
    uint8_t  free;
    uint8_t  pad[7];
    struct block *next;
};

#define HEADER_SIZE sizeof(struct block)

static struct block *free_list = NULL;

static struct block *extend_heap(size_t size) {
    size_t total = HEADER_SIZE + size;
    void *p = (void *)usbrk((int64_t)total);
    if ((int64_t)p == -1) return NULL;

    struct block *b = (struct block *)p;
    b->magic = BLOCK_MAGIC;
    b->size = (uint32_t)size;
    b->free = 0;
    b->next = NULL;

    if (!free_list) {
        free_list = b;
    } else {
        struct block *last = free_list;
        while (last->next) last = last->next;
        last->next = b;
    }

    return b;
}

void *malloc(size_t size) {
    if (size == 0) return NULL;
    size = ALIGN16(size);

    /* First-fit search */
    struct block *b = free_list;
    while (b) {
        if (b->free && b->size >= size) {
            /* Split if block is significantly larger */
            if (b->size >= size + HEADER_SIZE + 16) {
                struct block *new_block = (struct block *)((uint8_t *)b + HEADER_SIZE + size);
                new_block->magic = BLOCK_MAGIC;
                new_block->size = b->size - (uint32_t)size - (uint32_t)HEADER_SIZE;
                new_block->free = 1;
                new_block->next = b->next;
                b->size = (uint32_t)size;
                b->next = new_block;
            }
            b->free = 0;
            return (void *)((uint8_t *)b + HEADER_SIZE);
        }
        b = b->next;
    }

    /* No free block found — extend heap */
    b = extend_heap(size);
    if (!b) return NULL;
    return (void *)((uint8_t *)b + HEADER_SIZE);
}

void free(void *ptr) {
    if (!ptr) return;
    struct block *b = (struct block *)((uint8_t *)ptr - HEADER_SIZE);
    if (b->magic != BLOCK_MAGIC) return;
    b->free = 1;

    /* Coalesce with next block if free */
    if (b->next && b->next->free && b->next->magic == BLOCK_MAGIC) {
        b->size += (uint32_t)HEADER_SIZE + b->next->size;
        b->next = b->next->next;
    }
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return NULL; }

    struct block *b = (struct block *)((uint8_t *)ptr - HEADER_SIZE);
    if (b->magic != BLOCK_MAGIC) return NULL;
    if (b->size >= size) return ptr;

    void *new_ptr = malloc(size);
    if (!new_ptr) return NULL;
    ulib_memcpy(new_ptr, ptr, b->size);
    free(ptr);
    return new_ptr;
}
