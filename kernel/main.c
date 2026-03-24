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
#include "task/signal.h"
#include "shell/shell.h"
#include "fs/vfs.h"
#include "fs/fat.h"
#include "cpu/syscall.h"
#include "cpu/fpu.h"
#include "kernel/initrd.h"
#include "kernel/env.h"
#include "kernel/console.h"
#include "net/net.h"
#include "net/arp.h"
#include "net/tcp.h"
#include "net/dns.h"
#include "drivers/pci.h"
#include "drivers/ata.h"
#include "drivers/fb.h"
#include "drivers/mouse.h"
#include "mm/vma.h"
#include "fs/bcache.h"
#include "cpu/apic.h"

void kernel_main(uint64_t multiboot_info_addr) {
    /* Phase 1: Basic output */
    vga_init();
    serial_init();

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    kprintf("PlantOS v0.15 booting...\n");
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

    /* Phase 5.5: FPU/SSE */
    fpu_init();

    /* Phase 5.6: Shared memory */
    shm_init();

    /* Phase 6: Scheduler */
    sched_init();

    /* Phase 7: Filesystem */
    vfs_init();
    ramfs_init();

    /* Phase 8: PCI bus enumeration */
    pci_init();

    /* Phase 9: Framebuffer (Bochs VGA) */
    fb_init();

    /* Phase 10: PS/2 mouse */
    mouse_init();

    /* Phase 11: ATA disk driver + buffer cache */
    ata_init();
    bcache_init();

    /* Phase 10: FAT filesystem */
    fat_init();
    if (ata_is_present()) {
        if (fat_mount_first_partition() == 0) {
            vfs_mount_fat("/disk");
        }
    }

    /* Phase 11: Initial ramdisk */
    initrd_init();

    /* Phase 12: Signals */
    signal_init();

    /* Phase 13: Syscalls */
    syscall_init();

    /* Phase 13.5: Environment variables and cwd */
    env_init();

    /* Phase 13.6: Console input subsystem */
    console_init();

    /* Phase 14: Networking */
    arp_init();
    tcp_init();
    dns_init();
    net_init();

    /* Phase 15: SMP — detect and boot additional CPUs */
    apic_init();
    smp_boot_aps();

    /* Phase 16: Spawn shell as a task */
    task_create("shell", shell_task_entry);

    /* Enable interrupts — scheduler starts running */
    kprintf("[KERNEL] All systems go. Enabling interrupts.\n");
    __asm__ volatile ("sti");

    /* Kernel task becomes the idle loop — the scheduler will preempt us */
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
