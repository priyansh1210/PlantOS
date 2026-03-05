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
    if (table[index] & VMM_PRESENT) {
        /* If this is a 2MB huge page entry, split it into 512 x 4KB PTEs */
        if (table[index] & VMM_HUGE) {
            uint64_t huge_phys = table[index] & ~0x1FFFFFULL; /* 2MB-aligned base */
            uint64_t huge_end  = huge_phys + 0x200000ULL;
            uint64_t huge_flags = table[index] & 0x1FULL;     /* P, RW, US, etc. */
            /* Remove the HUGE bit for 4KB entries */
            huge_flags &= ~VMM_HUGE;

            /*
             * CRITICAL: The PT page must NOT be within the 2MB range being split.
             * If it were, remapping pages in this range would break the identity
             * mapping used to access the PT itself, corrupting page tables.
             */
            void *new_pt = NULL;
            void *rejected[16];
            int nrej = 0;
            for (int attempt = 0; attempt < 16; attempt++) {
                new_pt = pmm_alloc_page();
                if (!new_pt) {
                    for (int j = 0; j < nrej; j++) pmm_free_page(rejected[j]);
                    return NULL;
                }
                if ((uint64_t)new_pt >= huge_phys && (uint64_t)new_pt < huge_end) {
                    rejected[nrej++] = new_pt;
                    new_pt = NULL;
                    continue;
                }
                break;
            }
            for (int j = 0; j < nrej; j++) pmm_free_page(rejected[j]);
            if (!new_pt) return NULL;

            uint64_t *pt = (uint64_t *)new_pt;

            /* Fill 512 entries, each mapping 4KB of the original 2MB range */
            for (int i = 0; i < 512; i++) {
                pt[i] = (huge_phys + (uint64_t)i * PAGE_SIZE) | huge_flags;
            }

            /* Replace the PD entry: now points to a page table, not a huge page */
            table[index] = (uint64_t)new_pt | flags;
        }
    } else {
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

    /* Intermediate entries need USER bit if final mapping is user-accessible */
    uint64_t tbl_flags = VMM_PRESENT | VMM_WRITE;
    if (flags & VMM_USER) tbl_flags |= VMM_USER;

    uint64_t *pdpt = get_or_create_table(pml4, pml4_idx, tbl_flags);
    if (!pdpt) return;

    uint64_t *pd = get_or_create_table(pdpt, pdpt_idx, tbl_flags);
    if (!pd) return;

    uint64_t *pt = get_or_create_table(pd, pd_idx, tbl_flags);
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
    if (pd[pd_idx] & VMM_HUGE) return;  /* Cannot unmap a single 4KB page from a 2MB huge page */
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
