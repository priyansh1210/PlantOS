#include "mm/vmm.h"
#include "mm/pmm.h"
#include "lib/string.h"
#include "lib/printf.h"

static uint64_t boot_cr3 = 0;

/* Get current PML4 from CR3 */
static uint64_t *get_pml4(void) {
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return (uint64_t *)(cr3 & ~0xFFFULL);
}

/*
 * Get or create a sub-table entry. If the entry is a 2MB huge page,
 * split it into 512 x 4KB PTEs.
 */
static uint64_t *get_or_create_table(uint64_t *table, int index, uint64_t flags) {
    if (table[index] & VMM_PRESENT) {
        if (table[index] & VMM_HUGE) {
            uint64_t huge_phys = table[index] & ~0x1FFFFFULL;
            uint64_t huge_end  = huge_phys + 0x200000ULL;
            uint64_t huge_flags = table[index] & 0x1FULL;
            huge_flags &= ~VMM_HUGE;

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
            for (int i = 0; i < 512; i++) {
                pt[i] = (huge_phys + (uint64_t)i * PAGE_SIZE) | huge_flags;
            }
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
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    boot_cr3 = cr3 & ~0xFFFULL;
    kprintf("[VMM] Virtual memory manager initialized (boot CR3=0x%llx)\n", boot_cr3);
}

uint64_t vmm_get_boot_cr3(void) {
    return boot_cr3;
}

void vmm_switch_address_space(uint64_t pml4_phys) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
}

void vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t *pml4 = get_pml4();
    int pml4_idx = (virt >> 39) & 0x1FF;
    int pdpt_idx = (virt >> 30) & 0x1FF;
    int pd_idx   = (virt >> 21) & 0x1FF;
    int pt_idx   = (virt >> 12) & 0x1FF;

    uint64_t tbl_flags = VMM_PRESENT | VMM_WRITE;
    if (flags & VMM_USER) tbl_flags |= VMM_USER;

    uint64_t *pdpt = get_or_create_table(pml4, pml4_idx, tbl_flags);
    if (!pdpt) return;

    uint64_t *pd = get_or_create_table(pdpt, pdpt_idx, tbl_flags);
    if (!pd) return;

    uint64_t *pt = get_or_create_table(pd, pd_idx, tbl_flags);
    if (!pt) return;

    pt[pt_idx] = (phys & ~0xFFFULL) | flags;
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

void vmm_map_page_in(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t *pml4 = (uint64_t *)pml4_phys;
    int pml4_idx = (virt >> 39) & 0x1FF;
    int pdpt_idx = (virt >> 30) & 0x1FF;
    int pd_idx   = (virt >> 21) & 0x1FF;
    int pt_idx   = (virt >> 12) & 0x1FF;

    uint64_t tbl_flags = VMM_PRESENT | VMM_WRITE;
    if (flags & VMM_USER) tbl_flags |= VMM_USER;

    uint64_t *pdpt = get_or_create_table(pml4, pml4_idx, tbl_flags);
    if (!pdpt) return;

    uint64_t *pd = get_or_create_table(pdpt, pdpt_idx, tbl_flags);
    if (!pd) return;

    uint64_t *pt = get_or_create_table(pd, pd_idx, tbl_flags);
    if (!pt) return;

    pt[pt_idx] = (phys & ~0xFFFULL) | flags;
    /* No invlpg — target address space may not be active */
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
    if (pd[pd_idx] & VMM_HUGE) return;
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
    if (pdpt[pdpt_idx] & VMM_HUGE)
        return (pdpt[pdpt_idx] & ~0x3FFFFFFFULL) | (virt & 0x3FFFFFFFULL);

    uint64_t *pd = (uint64_t *)(pdpt[pdpt_idx] & ~0xFFFULL);

    if (!(pd[pd_idx] & VMM_PRESENT)) return 0;
    if (pd[pd_idx] & VMM_HUGE)
        return (pd[pd_idx] & ~0x1FFFFFULL) | (virt & 0x1FFFFFULL);

    uint64_t *pt = (uint64_t *)(pd[pd_idx] & ~0xFFFULL);
    if (!(pt[pt_idx] & VMM_PRESENT)) return 0;

    return (pt[pt_idx] & ~0xFFFULL) | (virt & 0xFFFULL);
}

uint64_t vmm_get_physical_in(uint64_t pml4_phys, uint64_t virt) {
    uint64_t *pml4 = (uint64_t *)pml4_phys;
    int pml4_idx = (virt >> 39) & 0x1FF;
    int pdpt_idx = (virt >> 30) & 0x1FF;
    int pd_idx   = (virt >> 21) & 0x1FF;
    int pt_idx   = (virt >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & VMM_PRESENT)) return 0;
    uint64_t *pdpt = (uint64_t *)(pml4[pml4_idx] & ~0xFFFULL);

    if (!(pdpt[pdpt_idx] & VMM_PRESENT)) return 0;
    if (pdpt[pdpt_idx] & VMM_HUGE)
        return (pdpt[pdpt_idx] & ~0x3FFFFFFFULL) | (virt & 0x3FFFFFFFULL);

    uint64_t *pd = (uint64_t *)(pdpt[pdpt_idx] & ~0xFFFULL);

    if (!(pd[pd_idx] & VMM_PRESENT)) return 0;
    if (pd[pd_idx] & VMM_HUGE)
        return (pd[pd_idx] & ~0x1FFFFFULL) | (virt & 0x1FFFFFULL);

    uint64_t *pt = (uint64_t *)(pd[pd_idx] & ~0xFFFULL);
    if (!(pt[pt_idx] & VMM_PRESENT)) return 0;

    return (pt[pt_idx] & ~0xFFFULL) | (virt & 0xFFFULL);
}

/* --- Per-process address space management --- */

uint64_t vmm_create_address_space(void) {
    /* Allocate PML4 */
    void *pml4_page = pmm_alloc_page();
    if (!pml4_page) return 0;
    memset(pml4_page, 0, PAGE_SIZE);
    uint64_t *pml4 = (uint64_t *)pml4_page;

    /* Get boot page table structure */
    uint64_t *boot_pml4 = (uint64_t *)boot_cr3;
    if (!(boot_pml4[0] & VMM_PRESENT)) {
        pmm_free_page(pml4_page);
        return 0;
    }
    uint64_t *boot_pdpt = (uint64_t *)(boot_pml4[0] & ~0xFFFULL);
    uint64_t pdpt_flags = boot_pml4[0] & 0xFFFULL;

    /* Allocate new PDPT */
    void *pdpt_page = pmm_alloc_page();
    if (!pdpt_page) {
        pmm_free_page(pml4_page);
        return 0;
    }
    memset(pdpt_page, 0, PAGE_SIZE);
    uint64_t *pdpt = (uint64_t *)pdpt_page;

    /* Copy each PD table (4 entries covering 4GB) */
    for (int i = 0; i < 4; i++) {
        if (!(boot_pdpt[i] & VMM_PRESENT)) continue;

        uint64_t *boot_pd = (uint64_t *)(boot_pdpt[i] & ~0xFFFULL);
        uint64_t pd_flags = boot_pdpt[i] & 0xFFFULL;

        void *pd_page = pmm_alloc_page();
        if (!pd_page) {
            /* Cleanup on failure */
            for (int j = 0; j < i; j++) {
                if (pdpt[j] & VMM_PRESENT)
                    pmm_free_page((void *)(pdpt[j] & ~0xFFFULL));
            }
            pmm_free_page(pdpt_page);
            pmm_free_page(pml4_page);
            return 0;
        }

        /* Copy all 512 PD entries (2MB huge pages from boot) */
        memcpy(pd_page, boot_pd, PAGE_SIZE);
        pdpt[i] = (uint64_t)pd_page | pd_flags;
    }

    pml4[0] = (uint64_t)pdpt_page | pdpt_flags;
    return (uint64_t)pml4_page;
}

void vmm_destroy_address_space(uint64_t pml4_phys) {
    if (pml4_phys == 0 || pml4_phys == boot_cr3) return;

    uint64_t *pml4 = (uint64_t *)pml4_phys;
    if (!(pml4[0] & VMM_PRESENT)) {
        pmm_free_page((void *)pml4_phys);
        return;
    }

    uint64_t *pdpt = (uint64_t *)(pml4[0] & ~0xFFFULL);

    for (int i = 0; i < 4; i++) {
        if (!(pdpt[i] & VMM_PRESENT)) continue;

        uint64_t *pd = (uint64_t *)(pdpt[i] & ~0xFFFULL);

        for (int j = 0; j < 512; j++) {
            if (!(pd[j] & VMM_PRESENT)) continue;
            if (pd[j] & VMM_HUGE) continue; /* Kernel 2MB page — don't free */

            /* This is a 4KB page table (split from a huge page for user mapping) */
            uint64_t *pt = (uint64_t *)(pd[j] & ~0xFFFULL);

            for (int k = 0; k < 512; k++) {
                if (!(pt[k] & VMM_PRESENT)) continue;

                uint64_t phys = pt[k] & ~0xFFFULL;
                uint64_t vaddr = ((uint64_t)i << 30) | ((uint64_t)j << 21) | ((uint64_t)k << 12);

                /* Only free user pages (non-identity-mapped) */
                if (phys != vaddr) {
                    pmm_free_page((void *)phys);
                }
            }

            /* Free the PT page itself */
            pmm_free_page((void *)(pd[j] & ~0xFFFULL));
        }

        /* Free the PD page */
        pmm_free_page((void *)(pdpt[i] & ~0xFFFULL));
    }

    /* Free PDPT and PML4 */
    pmm_free_page((void *)(pml4[0] & ~0xFFFULL));
    pmm_free_page((void *)pml4_phys);
}

uint64_t vmm_clone_address_space(uint64_t src_pml4_phys) {
    /* Create a fresh address space with kernel identity mapping */
    uint64_t new_pml4_phys = vmm_create_address_space();
    if (!new_pml4_phys) return 0;

    uint64_t *src_pml4 = (uint64_t *)src_pml4_phys;
    uint64_t *new_pml4 = (uint64_t *)new_pml4_phys;

    if (!(src_pml4[0] & VMM_PRESENT)) return new_pml4_phys;

    uint64_t *src_pdpt = (uint64_t *)(src_pml4[0] & ~0xFFFULL);
    uint64_t *new_pdpt = (uint64_t *)(new_pml4[0] & ~0xFFFULL);

    for (int i = 0; i < 4; i++) {
        if (!(src_pdpt[i] & VMM_PRESENT)) continue;

        uint64_t *src_pd = (uint64_t *)(src_pdpt[i] & ~0xFFFULL);
        uint64_t *new_pd = (uint64_t *)(new_pdpt[i] & ~0xFFFULL);

        for (int j = 0; j < 512; j++) {
            if (!(src_pd[j] & VMM_PRESENT)) continue;
            if (src_pd[j] & VMM_HUGE) continue; /* Kernel page — already copied */

            /* This is a PT — deep copy user pages */
            uint64_t *src_pt = (uint64_t *)(src_pd[j] & ~0xFFFULL);
            uint64_t pt_flags = src_pd[j] & 0xFFFULL;

            void *new_pt_page = pmm_alloc_page();
            if (!new_pt_page) {
                vmm_destroy_address_space(new_pml4_phys);
                return 0;
            }
            memset(new_pt_page, 0, PAGE_SIZE);
            uint64_t *new_pt = (uint64_t *)new_pt_page;

            for (int k = 0; k < 512; k++) {
                if (!(src_pt[k] & VMM_PRESENT)) continue;

                uint64_t src_phys = src_pt[k] & ~0xFFFULL;
                uint64_t entry_flags = src_pt[k] & 0xFFFULL;
                uint64_t vaddr = ((uint64_t)i << 30) | ((uint64_t)j << 21) | ((uint64_t)k << 12);

                if (src_phys == vaddr) {
                    /* Identity-mapped kernel page — share it */
                    new_pt[k] = src_pt[k];
                } else {
                    /* User page — allocate new physical page and copy data */
                    void *new_page = pmm_alloc_page();
                    if (!new_page) {
                        vmm_destroy_address_space(new_pml4_phys);
                        return 0;
                    }
                    memcpy(new_page, (void *)src_phys, PAGE_SIZE);
                    new_pt[k] = (uint64_t)new_page | entry_flags;
                }
            }

            new_pd[j] = (uint64_t)new_pt_page | pt_flags;
        }
    }

    return new_pml4_phys;
}
