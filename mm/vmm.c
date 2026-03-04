#include "mm/vmm.h"
#include "mm/pmm.h"
#include "lib/string.h"
#include "lib/printf.h"

/* Get current PML4 from CR3 */
static uint64_t *get_pml4(void) {
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return (uint64_t *)(cr3 & ~0xFFFULL);
}

static uint64_t *get_or_create_table(uint64_t *table, int index, uint64_t flags) {
    if (!(table[index] & VMM_PRESENT)) {
        void *page = pmm_alloc_page();
        if (!page) return NULL;
        memset(page, 0, PAGE_SIZE);
        table[index] = (uint64_t)page | flags;
    }
    return (uint64_t *)(table[index] & ~0xFFFULL);
}

void vmm_init(void) {
    kprintf("[VMM] Virtual memory manager initialized (using boot page tables)\n");
}

void vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t *pml4 = get_pml4();
    int pml4_idx = (virt >> 39) & 0x1FF;
    int pdpt_idx = (virt >> 30) & 0x1FF;
    int pd_idx   = (virt >> 21) & 0x1FF;
    int pt_idx   = (virt >> 12) & 0x1FF;

    uint64_t *pdpt = get_or_create_table(pml4, pml4_idx, VMM_PRESENT | VMM_WRITE);
    if (!pdpt) return;

    uint64_t *pd = get_or_create_table(pdpt, pdpt_idx, VMM_PRESENT | VMM_WRITE);
    if (!pd) return;

    uint64_t *pt = get_or_create_table(pd, pd_idx, VMM_PRESENT | VMM_WRITE);
    if (!pt) return;

    pt[pt_idx] = (phys & ~0xFFFULL) | flags;

    /* Invalidate TLB for this page */
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

void vmm_unmap_page(uint64_t virt) {
    uint64_t *pml4 = get_pml4();
    int pml4_idx = (virt >> 39) & 0x1FF;
    int pdpt_idx = (virt >> 30) & 0x1FF;
    int pd_idx   = (virt >> 21) & 0x1FF;
    int pt_idx   = (virt >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & VMM_PRESENT)) return;
    uint64_t *pdpt = (uint64_t *)(pml4[pml4_idx] & ~0xFFFULL);

    if (!(pdpt[pdpt_idx] & VMM_PRESENT)) return;
    uint64_t *pd = (uint64_t *)(pdpt[pdpt_idx] & ~0xFFFULL);

    if (!(pd[pd_idx] & VMM_PRESENT)) return;
    uint64_t *pt = (uint64_t *)(pd[pd_idx] & ~0xFFFULL);

    pt[pt_idx] = 0;
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

uint64_t vmm_get_physical(uint64_t virt) {
    uint64_t *pml4 = get_pml4();
    int pml4_idx = (virt >> 39) & 0x1FF;
    int pdpt_idx = (virt >> 30) & 0x1FF;
    int pd_idx   = (virt >> 21) & 0x1FF;
    int pt_idx   = (virt >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & VMM_PRESENT)) return 0;
    uint64_t *pdpt = (uint64_t *)(pml4[pml4_idx] & ~0xFFFULL);

    if (!(pdpt[pdpt_idx] & VMM_PRESENT)) return 0;
    /* Check for 1GB huge page */
    if (pdpt[pdpt_idx] & VMM_HUGE)
        return (pdpt[pdpt_idx] & ~0x3FFFFFFFULL) | (virt & 0x3FFFFFFFULL);

    uint64_t *pd = (uint64_t *)(pdpt[pdpt_idx] & ~0xFFFULL);

    if (!(pd[pd_idx] & VMM_PRESENT)) return 0;
    /* Check for 2MB huge page */
    if (pd[pd_idx] & VMM_HUGE)
        return (pd[pd_idx] & ~0x1FFFFFULL) | (virt & 0x1FFFFFULL);

    uint64_t *pt = (uint64_t *)(pd[pd_idx] & ~0xFFFULL);
    if (!(pt[pt_idx] & VMM_PRESENT)) return 0;

    return (pt[pt_idx] & ~0xFFFULL) | (virt & 0xFFFULL);
}
