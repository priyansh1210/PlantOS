#include "cpu/syscall.h"
#include "cpu/idt.h"
#include "drivers/vga.h"
#include "drivers/serial.h"
#include "lib/printf.h"
#include "task/task.h"
#include "fs/elf_loader.h"

void syscall_handler(struct registers *regs) {
    uint64_t num = regs->rax;
    uint64_t arg0 = regs->rdi;
    uint64_t arg1 = regs->rsi;
    uint64_t arg2 = regs->rdx;

    switch (num) {
    case SYS_WRITE: {
        /* sys_write(fd, buf, len) — fd=1 writes to VGA */
        int fd = (int)arg0;
        const char *buf = (const char *)arg1;
        uint64_t len = arg2;
        if (fd == 1) {
            for (uint64_t i = 0; i < len; i++) {
                vga_putchar(buf[i]);
                serial_putchar(buf[i]);
            }
            regs->rax = len;
        } else {
            regs->rax = (uint64_t)-1;
        }
        break;
    }
    case SYS_EXIT:
        task_exit();
        /* Does not return */
        break;
    case SYS_YIELD:
        /* Mark current task as ready so scheduler picks another.
         * The actual switch happens on the next timer IRQ. */
        {
            struct task *cur = task_current();
            if (cur->state == TASK_RUNNING)
                cur->state = TASK_READY;
        }
        regs->rax = 0;
        break;
    case SYS_GETPID:
        regs->rax = task_current()->pid;
        break;
    case SYS_EXEC: {
        const char *path = (const char *)arg0;
        struct elf_info info;
        if (elf_load(path, &info) < 0) {
            regs->rax = (uint64_t)-1;
        } else {
            struct task *t = task_create_user_elf(path, info.entry,
                                                  info.load_base, info.num_pages);
            if (t) {
                regs->rax = t->pid;
            } else {
                elf_unload(info.load_base, info.num_pages);
                regs->rax = (uint64_t)-1;
            }
        }
        break;
    }
    default:
        regs->rax = (uint64_t)-1;
        break;
    }
}

void syscall_init(void) {
    /* INT 0x80: DPL=3 interrupt gate (0xEE = present | DPL3 | interrupt gate) */
    idt_set_gate(0x80, (uint64_t)syscall_stub, 0x08, 0xEE);
    kprintf("[SYSCALL] Initialized (INT 0x80)\n");
}
