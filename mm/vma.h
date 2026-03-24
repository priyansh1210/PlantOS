#ifndef MM_VMA_H
#define MM_VMA_H

#include <plantos/types.h>

/* Virtual Memory Area flags */
#define VMA_READ    0x01
#define VMA_WRITE   0x02
#define VMA_EXEC    0x04
#define VMA_SHARED  0x08    /* Pages are shared (don't free on unmap) */
#define VMA_STACK   0x10    /* Stack region (grows down) */

/* VMA types */
#define VMA_TYPE_ANON   0   /* Anonymous: zero-filled on demand */
#define VMA_TYPE_HEAP   1   /* Heap region (sbrk-managed) */
#define VMA_TYPE_STACK  2   /* User stack (grows down on fault) */
#define VMA_TYPE_ELF    3   /* ELF-loaded region */
#define VMA_TYPE_SHM    4   /* Shared memory segment */

#define VMA_MAX_PER_TASK 32

/* mmap region: 0x10000000 to 0x70000000 */
#define MMAP_BASE   0x10000000ULL
#define MMAP_END    0x70000000ULL

/* Stack region: 0x700000 to 0x800000 (up to 1MB stack) */
#define STACK_GUARD_LOW  0x700000ULL

struct vma {
    uint64_t start;     /* Start virtual address (page-aligned) */
    uint64_t end;       /* End virtual address (exclusive, page-aligned) */
    uint32_t flags;     /* VMA_READ | VMA_WRITE | ... */
    uint32_t type;      /* VMA_TYPE_ANON, etc. */
    int      shm_id;    /* Shared memory segment ID (-1 if none) */
    bool     used;
};

/* Per-task VMA operations */
void vma_init_list(struct vma *vmas);
struct vma *vma_alloc(struct vma *vmas);
struct vma *vma_find(struct vma *vmas, uint64_t addr);
int  vma_add(struct vma *vmas, uint64_t start, uint64_t end,
             uint32_t flags, uint32_t type);
void vma_remove(struct vma *vmas, uint64_t start, uint64_t end);
void vma_copy(struct vma *dst, const struct vma *src);

/* Find free virtual address range in mmap region */
uint64_t vma_find_free(struct vma *vmas, uint64_t size);

/* Shared memory */
#define SHM_MAX_SEGMENTS 16
#define SHM_NAME_MAX     32
#define SHM_MAX_PAGES    256

struct shm_segment {
    char     name[SHM_NAME_MAX];
    uint64_t pages[SHM_MAX_PAGES];  /* Physical page addresses */
    uint32_t num_pages;
    uint32_t ref_count;
    bool     used;
};

void shm_init(void);
int  shm_create(const char *name, uint32_t num_pages);
int  shm_find(const char *name);
int  shm_ref(int id);
void shm_unref(int id);
struct shm_segment *shm_get(int id);

#endif
