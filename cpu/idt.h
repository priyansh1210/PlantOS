#ifndef CPU_IDT_H
#define CPU_IDT_H

#include <plantos/types.h>

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} PACKED;

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} PACKED;

/* Register state pushed by ISR/IRQ stubs */
struct registers {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
} PACKED;

typedef void (*isr_handler_t)(struct registers *regs);

void idt_init(void);
void idt_set_gate(int num, uint64_t handler, uint16_t selector, uint8_t type_attr);
void register_interrupt_handler(int num, isr_handler_t handler);
isr_handler_t get_interrupt_handler(int num);

/* Defined in idt_flush.asm */
extern void idt_flush(uint64_t idt_ptr);

#endif
