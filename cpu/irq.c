#include "cpu/irq.h"
#include "cpu/idt.h"
#include "drivers/pic.h"
#include "lib/printf.h"

void irq_init(void) {
    /* 0x8E = Present, DPL=0, 64-bit interrupt gate */
    idt_set_gate(32, (uint64_t)irq0,  0x08, 0x8E);
    idt_set_gate(33, (uint64_t)irq1,  0x08, 0x8E);
    idt_set_gate(34, (uint64_t)irq2,  0x08, 0x8E);
    idt_set_gate(35, (uint64_t)irq3,  0x08, 0x8E);
    idt_set_gate(36, (uint64_t)irq4,  0x08, 0x8E);
    idt_set_gate(37, (uint64_t)irq5,  0x08, 0x8E);
    idt_set_gate(38, (uint64_t)irq6,  0x08, 0x8E);
    idt_set_gate(39, (uint64_t)irq7,  0x08, 0x8E);
    idt_set_gate(40, (uint64_t)irq8,  0x08, 0x8E);
    idt_set_gate(41, (uint64_t)irq9,  0x08, 0x8E);
    idt_set_gate(42, (uint64_t)irq10, 0x08, 0x8E);
    idt_set_gate(43, (uint64_t)irq11, 0x08, 0x8E);
    idt_set_gate(44, (uint64_t)irq12, 0x08, 0x8E);
    idt_set_gate(45, (uint64_t)irq13, 0x08, 0x8E);
    idt_set_gate(46, (uint64_t)irq14, 0x08, 0x8E);
    idt_set_gate(47, (uint64_t)irq15, 0x08, 0x8E);

    kprintf("[IRQ] Hardware interrupt handlers installed\n");
}

/*
 * Called from assembly IRQ common stub.
 * Returns the RSP to restore — enables preemptive context switching.
 */
uint64_t irq_handler(uint64_t rsp) {
    struct registers *regs = (struct registers *)rsp;

    /* Call registered handler if any */
    isr_handler_t handler = get_interrupt_handler(regs->int_no);
    if (handler) {
        handler(regs);
    }

    /* Send EOI */
    pic_send_eoi(regs->int_no - 32);

    /* For IRQ0 (timer), the scheduler may return a different RSP */
    if (regs->int_no == 32) {
        extern uint64_t schedule_from_irq(uint64_t old_rsp);
        return schedule_from_irq(rsp);
    }

    return rsp;
}
