#ifndef MM_PMM_H
#define MM_PMM_H

#include <plantos/types.h>

#define PAGE_SIZE 4096

void pmm_init(uint64_t multiboot_info_addr);
void *pmm_alloc_page(void);
void  pmm_free_page(void *addr);
void  pmm_mark_page_used(uint64_t addr);
uint64_t pmm_get_total_pages(void);
uint64_t pmm_get_used_pages(void);
uint64_t pmm_get_free_pages(void);

#endif
