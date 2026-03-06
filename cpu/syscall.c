#include "cpu/syscall.h"
#include "cpu/idt.h"
#include "drivers/vga.h"
#include "drivers/serial.h"
#include "lib/printf.h"
#include "task/task.h"
#include "task/signal.h"
#include "fs/elf_loader.h"
#include "fs/vfs.h"
#include "fs/pipe.h"

void syscall_handler(struct registers *regs) {
    uint64_t num = regs->rax;
    uint64_t arg0 = regs->rdi;
    uint64_t arg1 = regs->rsi;
    uint64_t arg2 = regs->rdx;

    switch (num) {
    case SYS_WRITE: {
        /* sys_write(fd, buf, len) */
        int fd = (int)arg0;
        const char *buf = (const char *)arg1;
        uint64_t len = arg2;
        if (fd == 1) {
            /* stdout: write to VGA + serial */
            for (uint64_t i = 0; i < len; i++) {
                vga_putchar(buf[i]);
                serial_putchar(buf[i]);
            }
            regs->rax = len;
        } else {
            /* Try pipe write first */
            int result = pipe_fd_write(fd, buf, (size_t)len);
            if (result == -2) {
                /* Not a pipe, try VFS */
                result = vfs_write(fd, buf, (size_t)len);
            }
            regs->rax = (uint64_t)result;
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
    case SYS_KILL: {
        uint64_t pid = arg0;
        int signum = (int)arg1;
        regs->rax = (uint64_t)signal_send(pid, signum);
        break;
    }
    case SYS_SIGNAL: {
        int signum = (int)arg0;
        uint64_t handler = arg1;
        regs->rax = (uint64_t)signal_register(signum, handler);
        break;
    }
    case SYS_SIGRETURN:
        signal_return(regs);
        break;
    case SYS_PIPE: {
        int *user_fds = (int *)arg0;
        int read_fd, write_fd;
        int ret = pipe_create(&read_fd, &write_fd);
        if (ret == 0) {
            user_fds[0] = read_fd;
            user_fds[1] = write_fd;
        }
        regs->rax = (uint64_t)ret;
        break;
    }
    case SYS_READ: {
        int fd = (int)arg0;
        void *buf = (void *)arg1;
        size_t count = (size_t)arg2;
        /* Check if this FD is a pipe */
        int result = pipe_fd_read(fd, buf, count);
        if (result == -2) {
            /* Not a pipe FD, use VFS */
            result = vfs_read(fd, buf, count);
        }
        regs->rax = (uint64_t)result;
        break;
    }
    case SYS_CLOSE: {
        int fd = (int)arg0;
        /* Try pipe close first */
        int result = pipe_fd_close(fd);
        if (result == -2) {
            /* Not a pipe FD, use VFS */
            result = vfs_close(fd);
        }
        regs->rax = (uint64_t)result;
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
