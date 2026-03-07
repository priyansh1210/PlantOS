#ifndef MM_VMM_H
#define MM_VMM_H

#include <plantos/types.h>

#define VMM_PRESENT  (1ULL << 0)
#define VMM_WRITE    (1ULL << 1)
#define VMM_USER     (1ULL << 2)
#define VMM_HUGE     (1ULL << 7)

void vmm_init(void);
void vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
void vmm_unmap_page(uint64_t virt);
uint64_t vmm_get_physical(uint64_t virt);

/* Per-process address spaces */
uint64_t vmm_get_boot_cr3(void);
uint64_t vmm_create_address_space(void);
void     vmm_destroy_address_space(uint64_t pml4_phys);
uint64_t vmm_clone_address_space(uint64_t src_pml4_phys);
void     vmm_switch_address_space(uint64_t pml4_phys);

/* Operate on a specific address space (without switching CR3) */
void     vmm_map_page_in(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags);
uint64_t vmm_get_physical_in(uint64_t pml4_phys, uint64_t virt);

#endif
