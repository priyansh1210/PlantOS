#include "mm/vma.h"
#include "mm/pmm.h"
#include "lib/string.h"
#include "lib/printf.h"

/* ---- Per-task VMA management ---- */

void vma_init_list(struct vma *vmas) {
    memset(vmas, 0, sizeof(struct vma) * VMA_MAX_PER_TASK);
}

struct vma *vma_alloc(struct vma *vmas) {
    for (int i = 0; i < VMA_MAX_PER_TASK; i++) {
        if (!vmas[i].used) return &vmas[i];
    }
    return NULL;
}

struct vma *vma_find(struct vma *vmas, uint64_t addr) {
    for (int i = 0; i < VMA_MAX_PER_TASK; i++) {
        if (vmas[i].used && addr >= vmas[i].start && addr < vmas[i].end)
            return &vmas[i];
    }
    return NULL;
}

int vma_add(struct vma *vmas, uint64_t start, uint64_t end,
            uint32_t flags, uint32_t type) {
    struct vma *v = vma_alloc(vmas);
    if (!v) return -1;
    v->start = start;
    v->end = end;
    v->flags = flags;
    v->type = type;
    v->shm_id = -1;
    v->used = true;
    return 0;
}

void vma_remove(struct vma *vmas, uint64_t start, uint64_t end) {
    for (int i = 0; i < VMA_MAX_PER_TASK; i++) {
        if (!vmas[i].used) continue;
        /* Remove VMAs that are fully contained in [start, end) */
        if (vmas[i].start >= start && vmas[i].end <= end) {
            vmas[i].used = false;
        }
    }
}

void vma_copy(struct vma *dst, const struct vma *src) {
    memcpy(dst, src, sizeof(struct vma) * VMA_MAX_PER_TASK);
}

uint64_t vma_find_free(struct vma *vmas, uint64_t size) {
    /* Simple first-fit in the mmap region */
    uint64_t addr = MMAP_BASE;
    size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);  /* Page-align */

    while (addr + size <= MMAP_END) {
        bool overlap = false;
        for (int i = 0; i < VMA_MAX_PER_TASK; i++) {
            if (!vmas[i].used) continue;
            /* Check if [addr, addr+size) overlaps this VMA */
            if (addr < vmas[i].end && (addr + size) > vmas[i].start) {
                addr = (vmas[i].end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
                overlap = true;
                break;
            }
        }
        if (!overlap) return addr;
    }
    return 0; /* No space found */
}

/* ---- Shared memory segments ---- */

static struct shm_segment shm_table[SHM_MAX_SEGMENTS];

void shm_init(void) {
    memset(shm_table, 0, sizeof(shm_table));
    kprintf("[SHM] Shared memory initialized\n");
}

int shm_create(const char *name, uint32_t num_pages) {
    if (num_pages == 0 || num_pages > SHM_MAX_PAGES) return -1;

    /* Check if already exists */
    int existing = shm_find(name);
    if (existing >= 0) return existing;

    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < SHM_MAX_SEGMENTS; i++) {
        if (!shm_table[i].used) { slot = i; break; }
    }
    if (slot < 0) return -1;

    struct shm_segment *seg = &shm_table[slot];
    strncpy(seg->name, name, SHM_NAME_MAX - 1);
    seg->name[SHM_NAME_MAX - 1] = '\0';
    seg->num_pages = num_pages;
    seg->ref_count = 0;
    seg->used = true;

    /* Allocate physical pages */
    for (uint32_t i = 0; i < num_pages; i++) {
        void *page = pmm_alloc_page();
        if (!page) {
            /* Cleanup on failure */
            for (uint32_t j = 0; j < i; j++)
                pmm_free_page((void *)seg->pages[j]);
            seg->used = false;
            return -1;
        }
        memset(page, 0, PAGE_SIZE);
        seg->pages[i] = (uint64_t)page;
    }

    return slot;
}

int shm_find(const char *name) {
    for (int i = 0; i < SHM_MAX_SEGMENTS; i++) {
        if (shm_table[i].used && strcmp(shm_table[i].name, name) == 0)
            return i;
    }
    return -1;
}

int shm_ref(int id) {
    if (id < 0 || id >= SHM_MAX_SEGMENTS || !shm_table[id].used) return -1;
    shm_table[id].ref_count++;
    return 0;
}

void shm_unref(int id) {
    if (id < 0 || id >= SHM_MAX_SEGMENTS || !shm_table[id].used) return;
    if (shm_table[id].ref_count > 0)
        shm_table[id].ref_count--;

    /* Free segment if no more references */
    if (shm_table[id].ref_count == 0) {
        for (uint32_t i = 0; i < shm_table[id].num_pages; i++)
            pmm_free_page((void *)shm_table[id].pages[i]);
        shm_table[id].used = false;
    }
}

struct shm_segment *shm_get(int id) {
    if (id < 0 || id >= SHM_MAX_SEGMENTS || !shm_table[id].used) return NULL;
    return &shm_table[id];
}
