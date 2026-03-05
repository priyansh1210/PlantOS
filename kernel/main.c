#include <plantos/types.h>
#include "drivers/vga.h"
#include "drivers/serial.h"
#include "drivers/pic.h"
#include "drivers/pit.h"
#include "drivers/keyboard.h"
#include "cpu/gdt.h"
#include "cpu/idt.h"
#include "cpu/isr.h"
#include "cpu/irq.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/heap.h"
#include "lib/printf.h"
#include "task/task.h"
#include "shell/shell.h"
#include "fs/vfs.h"
#include "cpu/syscall.h"
#include "kernel/initrd.h"

void kernel_main(uint64_t multiboot_info_addr) {
    /* Phase 1: Basic output */
    vga_init();
    serial_init();

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    kprintf("PlantOS v0.4 booting...\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    /* Phase 2: CPU tables */
    gdt_init();
    idt_init();
    isr_init();

    /* Phase 3: Hardware interrupts */
    pic_init();
    irq_init();

    /* Phase 4: Timer and keyboard */
    pit_init();
    keyboard_init();

    /* Phase 5: Memory management */
    pmm_init(multiboot_info_addr);
    vmm_init();
    heap_init();

    /* Phase 6: Scheduler */
    sched_init();

    /* Phase 7: Filesystem */
    vfs_init();
    ramfs_init();

    /* Phase 8: Initial ramdisk */
    initrd_init();

    /* Phase 9: Syscalls */
    syscall_init();

    /* Phase 9: Spawn shell as a task */
    task_create("shell", shell_task_entry);

    /* Enable interrupts — scheduler starts running */
    kprintf("[KERNEL] All systems go. Enabling interrupts.\n");
    __asm__ volatile ("sti");

    /* Kernel task becomes the idle loop — the scheduler will preempt us */
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
