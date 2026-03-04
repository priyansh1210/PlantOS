#include "kernel/panic.h"
#include "lib/printf.h"
#include "drivers/vga.h"

void kernel_panic(const char *msg) {
    __asm__ volatile ("cli");
    vga_set_color(VGA_WHITE, VGA_RED);
    kprintf("\n*** KERNEL PANIC: %s ***\n", msg);
    kprintf("System halted.\n");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
