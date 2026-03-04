#include "cpu/idt.h"
#include "lib/printf.h"
#include "lib/string.h"

#define IDT_ENTRIES 256

static struct idt_entry idt_entries[IDT_ENTRIES];
static struct idt_ptr   idt_pointer;
static isr_handler_t    interrupt_handlers[IDT_ENTRIES];

void idt_set_gate(int num, uint64_t handler, uint16_t selector, uint8_t type_attr) {
    idt_entries[num].offset_low  = handler & 0xFFFF;
    idt_entries[num].offset_mid  = (handler >> 16) & 0xFFFF;
    idt_entries[num].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt_entries[num].selector    = selector;
    idt_entries[num].ist         = 0;
    idt_entries[num].type_attr   = type_attr;
    idt_entries[num].reserved    = 0;
}

void idt_init(void) {
    idt_pointer.limit = sizeof(idt_entries) - 1;
    idt_pointer.base  = (uint64_t)&idt_entries;

    memset(idt_entries, 0, sizeof(idt_entries));
    memset(interrupt_handlers, 0, sizeof(interrupt_handlers));

    idt_flush((uint64_t)&idt_pointer);
    kprintf("[IDT] Initialized with %d entries\n", IDT_ENTRIES);
}

void register_interrupt_handler(int num, isr_handler_t handler) {
    interrupt_handlers[num] = handler;
}

isr_handler_t get_interrupt_handler(int num) {
    return interrupt_handlers[num];
}
