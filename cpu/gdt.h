#ifndef CPU_GDT_H
#define CPU_GDT_H

#include <plantos/types.h>

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} PACKED;

struct tss {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} PACKED;

void gdt_init(void);
void tss_set_rsp0(uint64_t rsp0);

/* Defined in gdt_flush.asm */
extern void gdt_flush(uint64_t gdt_ptr);
extern void tss_load(uint16_t selector);

#endif
