#include "cpu/gdt.h"
#include "lib/printf.h"
#include "lib/string.h"

/*
 * GDT layout (7 slots, 56 bytes):
 *   0: Null
 *   1: Kernel code (0x08)
 *   2: Kernel data (0x10)
 *   3: User code   (0x18) — but sysret/iretq uses 0x1B (RPL=3)
 *   4: User data   (0x20) — but iretq uses 0x23 (RPL=3)
 *   5-6: TSS descriptor (16 bytes, selector 0x28)
 */
static uint8_t gdt_bytes[7 * 8];
static struct gdt_ptr gdt_pointer;

struct tss kernel_tss;

static void gdt_set_gate(int slot, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    uint8_t *p = &gdt_bytes[slot * 8];
    /* limit low */
    p[0] = limit & 0xFF;
    p[1] = (limit >> 8) & 0xFF;
    /* base low */
    p[2] = base & 0xFF;
    p[3] = (base >> 8) & 0xFF;
    /* base middle */
    p[4] = (base >> 16) & 0xFF;
    /* access */
    p[5] = access;
    /* granularity + limit high */
    p[6] = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    /* base high */
    p[7] = (base >> 24) & 0xFF;
}

static void gdt_set_tss(int slot, uint64_t base, uint32_t limit) {
    uint8_t *p = &gdt_bytes[slot * 8];

    /* Low 8 bytes (like a normal descriptor) */
    p[0] = limit & 0xFF;
    p[1] = (limit >> 8) & 0xFF;
    p[2] = base & 0xFF;
    p[3] = (base >> 8) & 0xFF;
    p[4] = (base >> 16) & 0xFF;
    p[5] = 0x89; /* Present, DPL=0, type=9 (64-bit TSS available) */
    p[6] = (limit >> 16) & 0x0F; /* granularity=0, no flags needed */
    p[7] = (base >> 24) & 0xFF;

    /* High 8 bytes (base[63:32] + reserved) */
    p[8]  = (base >> 32) & 0xFF;
    p[9]  = (base >> 40) & 0xFF;
    p[10] = (base >> 48) & 0xFF;
    p[11] = (base >> 56) & 0xFF;
    p[12] = 0;
    p[13] = 0;
    p[14] = 0;
    p[15] = 0;
}

void tss_set_rsp0(uint64_t rsp0) {
    kernel_tss.rsp0 = rsp0;
}

void gdt_init(void) {
    memset(gdt_bytes, 0, sizeof(gdt_bytes));
    memset(&kernel_tss, 0, sizeof(kernel_tss));
    kernel_tss.iopb_offset = sizeof(struct tss);

    gdt_set_gate(0, 0, 0, 0, 0);                /* Null */
    gdt_set_gate(1, 0, 0xFFFFF, 0x9A, 0xA0);    /* Kernel code: 64-bit */
    gdt_set_gate(2, 0, 0xFFFFF, 0x92, 0xC0);    /* Kernel data */
    gdt_set_gate(3, 0, 0xFFFFF, 0xFA, 0xA0);    /* User code: 64-bit */
    gdt_set_gate(4, 0, 0xFFFFF, 0xF2, 0xC0);    /* User data */

    /* TSS descriptor at slot 5-6 (selector 0x28) */
    gdt_set_tss(5, (uint64_t)&kernel_tss, sizeof(struct tss) - 1);

    gdt_pointer.limit = sizeof(gdt_bytes) - 1;
    gdt_pointer.base  = (uint64_t)&gdt_bytes;

    gdt_flush((uint64_t)&gdt_pointer);
    tss_load(0x28);
    kprintf("[GDT] Initialized with 5 entries + TSS\n");
}
