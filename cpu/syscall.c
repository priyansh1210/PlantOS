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
#include "mm/vmm.h"
#include "mm/pmm.h"
#include "mm/vma.h"
#include "drivers/fb.h"
#include "kernel/env.h"
#include "kernel/console.h"
#include "net/tcp.h"
#include "net/udp.h"
#include "net/net.h"
#include "net/dns.h"

/* ---- Per-process fd helpers ---- */

/* Translate a user process fd to a global OFD index.
 * Returns: >= 0 global OFD, OFD_CONSOLE_IN/OUT for console, -1 on error. */
static int proc_to_ofd(int proc_fd) {
    struct task *t = task_current();
    if (proc_fd < 0 || proc_fd >= PROC_MAX_FDS)
        return -1;
    return t->ofd[proc_fd];
}

/* Allocate a free slot in the current task's fd table */
static int proc_alloc_fd(void) {
    struct task *t = task_current();
    for (int i = 0; i < PROC_MAX_FDS; i++) {
        if (t->ofd[i] == OFD_NONE)
            return i;
    }
    return -1;
}

/* Install a global OFD in the process fd table, return process fd */
static int proc_install_fd(int global_ofd) {
    int pfd = proc_alloc_fd();
    if (pfd < 0) return -1;
    task_current()->ofd[pfd] = global_ofd;
    return pfd;
}

void syscall_handler(struct registers *regs) {
    uint64_t num = regs->rax;
    uint64_t arg0 = regs->rdi;
    uint64_t arg1 = regs->rsi;
    uint64_t arg2 = regs->rdx;

    switch (num) {
    case SYS_WRITE: {
        int proc_fd = (int)arg0;
        const char *buf = (const char *)arg1;
        uint64_t len = arg2;
        int ofd = proc_to_ofd(proc_fd);

        if (ofd == OFD_CONSOLE_OUT) {
            /* stdout/stderr → VGA + serial */
            for (uint64_t i = 0; i < len; i++) {
                vga_putchar(buf[i]);
                serial_putchar(buf[i]);
            }
            regs->rax = len;
        } else if (ofd >= 0) {
            /* Try pipe first, then VFS */
            int result = pipe_fd_write(ofd, buf, (size_t)len);
            if (result == -2)
                result = vfs_write(ofd, buf, (size_t)len);
            regs->rax = (uint64_t)result;
        } else {
            regs->rax = (uint64_t)-1;
        }
        break;
    }
    case SYS_EXIT: {
        int code = (int)arg0;
        struct task *t = task_current();
        t->exit_code = code;
        task_exit();
        break;
    }
    case SYS_YIELD:
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
        uint64_t new_cr3 = vmm_create_address_space();
        if (!new_cr3) {
            regs->rax = (uint64_t)-1;
            break;
        }
        struct elf_info info;
        if (elf_load(path, &info, new_cr3) < 0) {
            vmm_destroy_address_space(new_cr3);
            regs->rax = (uint64_t)-1;
        } else {
            struct task *t = task_create_user_elf(path, info.entry,
                                                  info.load_base, info.num_pages,
                                                  new_cr3, 0, NULL);
            if (t) {
                regs->rax = t->pid;
            } else {
                vmm_destroy_address_space(new_cr3);
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
        int read_ofd, write_ofd;
        int ret = pipe_create(&read_ofd, &write_ofd);
        if (ret == 0) {
            /* Install in process fd table */
            int pfd_r = proc_install_fd(read_ofd);
            int pfd_w = proc_install_fd(write_ofd);
            if (pfd_r < 0 || pfd_w < 0) {
                /* Cleanup on failure */
                if (pfd_r >= 0) task_current()->ofd[pfd_r] = OFD_NONE;
                if (pfd_w >= 0) task_current()->ofd[pfd_w] = OFD_NONE;
                pipe_fd_close(read_ofd);
                pipe_fd_close(write_ofd);
                ret = -1;
            } else {
                user_fds[0] = pfd_r;
                user_fds[1] = pfd_w;
            }
        }
        regs->rax = (uint64_t)ret;
        break;
    }
    case SYS_READ: {
        int proc_fd = (int)arg0;
        void *buf = (void *)arg1;
        size_t count = (size_t)arg2;
        int ofd = proc_to_ofd(proc_fd);

        if (ofd == OFD_CONSOLE_IN) {
            /* stdin — read from console input buffer (blocking) */
            regs->rax = (uint64_t)console_read((char *)buf, (int)count);
        } else if (ofd >= 0) {
            int result = pipe_fd_read(ofd, buf, count);
            if (result == -2)
                result = vfs_read(ofd, buf, count);
            regs->rax = (uint64_t)result;
        } else {
            regs->rax = (uint64_t)-1;
        }
        break;
    }
    case SYS_CLOSE: {
        int proc_fd = (int)arg0;
        int ofd = proc_to_ofd(proc_fd);

        if (ofd == OFD_CONSOLE_IN || ofd == OFD_CONSOLE_OUT) {
            /* Closing console fd — just clear the slot */
            task_current()->ofd[proc_fd] = OFD_NONE;
            regs->rax = 0;
        } else if (ofd >= 0) {
            task_current()->ofd[proc_fd] = OFD_NONE;
            regs->rax = (uint64_t)vfs_close_fd(ofd);
        } else {
            regs->rax = (uint64_t)-1;
        }
        break;
    }
    case SYS_FORK: {
        struct task *child = task_fork(regs);
        if (child) {
            regs->rax = child->pid; /* Parent gets child PID */
        } else {
            regs->rax = (uint64_t)-1;
        }
        break;
    }
    case SYS_WAITPID: {
        uint64_t child_pid = arg0;
        regs->rax = (uint64_t)task_waitpid(child_pid);
        break;
    }
    case SYS_SBRK: {
        int64_t increment = (int64_t)arg0;
        struct task *t = task_current();
        if (!t->cr3 || !t->is_user) {
            regs->rax = (uint64_t)-1;
            break;
        }
        if (increment == 0) {
            regs->rax = t->brk;
            break;
        }
        if (increment < 0) {
            regs->rax = (uint64_t)-1;
            break;
        }
        uint64_t old_brk = t->brk;
        uint64_t new_brk = old_brk + (uint64_t)increment;
        uint64_t old_end = (old_brk + 0xFFF) & ~0xFFFULL;
        uint64_t new_end = (new_brk + 0xFFF) & ~0xFFFULL;
        for (uint64_t addr = old_end; addr < new_end; addr += 4096) {
            void *phys = pmm_alloc_page();
            if (!phys) {
                regs->rax = (uint64_t)-1;
                break;
            }
            for (int i = 0; i < 4096; i++)
                ((uint8_t *)phys)[i] = 0;
            vmm_map_page_in(t->cr3, addr, (uint64_t)phys,
                           VMM_PRESENT | VMM_WRITE | VMM_USER);
        }
        t->brk = new_brk;
        regs->rax = old_brk;
        break;
    }
    case SYS_MMAP: {
        uint64_t addr_hint = arg0;
        uint64_t length = arg1;
        uint64_t prot = arg2;
        struct task *t = task_current();
        if (!t->cr3 || !t->is_user || length == 0) {
            regs->rax = (uint64_t)-1;
            break;
        }
        length = (length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        uint64_t addr;
        if (addr_hint >= MMAP_BASE && addr_hint < MMAP_END) {
            if (!vma_find(t->vmas, addr_hint))
                addr = addr_hint;
            else
                addr = vma_find_free(t->vmas, length);
        } else {
            addr = vma_find_free(t->vmas, length);
        }
        if (addr == 0) {
            regs->rax = (uint64_t)-1;
            break;
        }
        uint32_t vma_flags = VMA_READ;
        if (prot & 0x02) vma_flags |= VMA_WRITE;
        if (prot & 0x04) vma_flags |= VMA_EXEC;
        if (vma_add(t->vmas, addr, addr + length, vma_flags, VMA_TYPE_ANON) < 0) {
            regs->rax = (uint64_t)-1;
            break;
        }
        uint64_t map_flags = VMM_PRESENT | VMM_USER;
        if (prot & 0x02) map_flags |= VMM_WRITE;
        int alloc_fail = 0;
        for (uint64_t va = addr; va < addr + length; va += PAGE_SIZE) {
            void *phys = pmm_alloc_page();
            if (!phys) { alloc_fail = 1; break; }
            for (int z = 0; z < 4096; z++)
                ((uint8_t *)phys)[z] = 0;
            vmm_map_page_in(t->cr3, va, (uint64_t)phys, map_flags);
        }
        if (alloc_fail) {
            for (uint64_t va = addr; va < addr + length; va += PAGE_SIZE) {
                uint64_t ph = vmm_get_physical_in(t->cr3, va);
                if (ph && ph != va)
                    pmm_free_page((void *)ph);
            }
            vma_remove(t->vmas, addr, addr + length);
            regs->rax = (uint64_t)-1;
            break;
        }
        regs->rax = addr;
        break;
    }
    case SYS_MUNMAP: {
        uint64_t addr = arg0;
        uint64_t length = arg1;
        struct task *t = task_current();
        if (!t->cr3 || !t->is_user) {
            regs->rax = (uint64_t)-1;
            break;
        }
        length = (length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        addr &= ~(PAGE_SIZE - 1);
        for (uint64_t va = addr; va < addr + length; va += PAGE_SIZE) {
            uint64_t phys = vmm_get_physical(va);
            if (phys && phys != va) {
                pmm_free_page((void *)phys);
                vmm_unmap_page(va);
            }
        }
        vma_remove(t->vmas, addr, addr + length);
        regs->rax = 0;
        break;
    }
    case SYS_OPEN: {
        const char *path = (const char *)arg0;
        int flags = (int)arg1;
        int vfs_flags = 0;
        if (flags & 0x40) vfs_flags |= VFS_O_CREATE;
        if (flags & 0x200) vfs_flags |= VFS_O_TRUNC;  /* O_TRUNC = 0x200 */
        int global_fd = vfs_open(path, vfs_flags);
        if (global_fd < 0) {
            regs->rax = (uint64_t)-1;
        } else {
            int pfd = proc_install_fd(global_fd);
            if (pfd < 0) {
                vfs_close(global_fd);
                regs->rax = (uint64_t)-1;
            } else {
                regs->rax = (uint64_t)pfd;
            }
        }
        break;
    }
    case SYS_FSTAT: {
        const char *path = (const char *)arg0;
        uint32_t *user_st = (uint32_t *)arg1;
        struct vfs_stat st;
        int ret = vfs_stat(path, &st);
        if (ret == 0 && user_st) {
            user_st[0] = st.type;
            user_st[1] = st.size;
        }
        regs->rax = (uint64_t)ret;
        break;
    }
    case SYS_MKDIR: {
        const char *path = (const char *)arg0;
        regs->rax = (uint64_t)vfs_mkdir(path);
        break;
    }
    case SYS_FBCTL: {
        uint64_t subcmd = arg0;
        switch (subcmd) {
        case 0: {
            struct fb_info *info = (struct fb_info *)arg1;
            if (info && fb_is_available()) {
                fb_get_info(info);
                regs->rax = 0;
            } else {
                regs->rax = (uint64_t)-1;
            }
            break;
        }
        case 1:
            regs->rax = (uint64_t)fb_set_mode((uint32_t)arg1, (uint32_t)arg2);
            break;
        case 2:
            fb_text_mode();
            regs->rax = 0;
            break;
        default:
            regs->rax = (uint64_t)-1;
            break;
        }
        break;
    }
    case SYS_LSEEK: {
        int proc_fd = (int)arg0;
        int offset = (int)arg1;
        int whence = (int)arg2;
        int ofd = proc_to_ofd(proc_fd);
        if (ofd >= 0)
            regs->rax = (uint64_t)vfs_lseek(ofd, offset, whence);
        else
            regs->rax = (uint64_t)-1;
        break;
    }
    case SYS_DUP2: {
        int old_pfd = (int)arg0;
        int new_pfd = (int)arg1;
        struct task *t = task_current();

        if (old_pfd < 0 || old_pfd >= PROC_MAX_FDS ||
            new_pfd < 0 || new_pfd >= PROC_MAX_FDS) {
            regs->rax = (uint64_t)-1;
            break;
        }

        int old_ofd = t->ofd[old_pfd];
        if (old_ofd == OFD_NONE) {
            regs->rax = (uint64_t)-1;
            break;
        }

        /* Close what's currently at new_pfd */
        if (t->ofd[new_pfd] >= 0)
            vfs_close_fd(t->ofd[new_pfd]);

        /* Point new_pfd to same OFD and bump refcount */
        t->ofd[new_pfd] = old_ofd;
        if (old_ofd >= 0)
            vfs_fd_addref(old_ofd);

        regs->rax = (uint64_t)new_pfd;
        break;
    }
    case SYS_GETCWD: {
        char *buf = (char *)arg0;
        size_t size = (size_t)arg1;
        const char *cwd = cwd_get();
        size_t len = 0;
        while (cwd[len]) len++;
        if (len + 1 > size) {
            regs->rax = (uint64_t)-1;
        } else {
            for (size_t i = 0; i <= len; i++) buf[i] = cwd[i];
            regs->rax = (uint64_t)buf;
        }
        break;
    }
    case SYS_CHDIR: {
        const char *path = (const char *)arg0;
        regs->rax = (uint64_t)cwd_set(path);
        break;
    }
    case SYS_GETENV: {
        const char *key = (const char *)arg0;
        const char *val = env_get(key);
        regs->rax = (uint64_t)val;
        break;
    }
    case SYS_SETENV: {
        const char *key = (const char *)arg0;
        const char *val = (const char *)arg1;
        regs->rax = (uint64_t)env_set(key, val);
        break;
    }
    case SYS_READDIR: {
        int proc_fd = (int)arg0;
        struct vfs_dirent *de = (struct vfs_dirent *)arg1;
        int ofd = proc_to_ofd(proc_fd);
        if (ofd >= 0)
            regs->rax = (uint64_t)vfs_readdir(ofd, de);
        else
            regs->rax = (uint64_t)-1;
        break;
    }
    case SYS_SOCKET: {
        /* Socket subcmds: arg0=subcmd, arg1/arg2=params */
        uint64_t subcmd = arg0;
        switch (subcmd) {
        case 0: { /* socket(domain, type, protocol) → sock_fd */
            /* domain(arg1): AF_INET=2, type(arg2): SOCK_STREAM=1, SOCK_DGRAM=2 */
            /* Returns a "socket fd" (negative = TCP conn index, encoded) */
            /* We encode socket fd as -(conn_index + 100) to avoid collision */
            regs->rax = 0; /* Success placeholder — real fd allocated on connect/bind */
            break;
        }
        case 1: { /* connect(ip, port) → 0 or -1 */
            uint32_t dst_ip = (uint32_t)arg1;
            uint16_t dst_port = (uint16_t)arg2;
            int conn = tcp_connect(dst_ip, dst_port);
            regs->rax = (uint64_t)(int64_t)conn;
            break;
        }
        case 2: { /* send(conn, buf, len) */
            int conn = (int)arg1;
            /* buf and len packed: buf in arg2 upper, len in lower — or use 2 args */
            /* Actually we need 3 args. Let's use: subcmd=2, arg1=conn|(len<<32), arg2=buf */
            uint16_t len = (uint16_t)(arg1 >> 32);
            int conn_id = (int)(arg1 & 0xFFFFFFFF);
            const void *buf = (const void *)arg2;
            regs->rax = (uint64_t)(int64_t)tcp_send(conn_id, buf, len);
            break;
        }
        case 3: { /* recv(conn, buf, len) */
            uint16_t len = (uint16_t)(arg1 >> 32);
            int conn_id = (int)(arg1 & 0xFFFFFFFF);
            void *buf = (void *)arg2;
            regs->rax = (uint64_t)(int64_t)tcp_recv(conn_id, buf, len);
            break;
        }
        case 4: { /* close(conn) */
            tcp_close((int)arg1);
            regs->rax = 0;
            break;
        }
        case 5: { /* listen(port) → conn */
            uint16_t port = (uint16_t)arg1;
            regs->rax = (uint64_t)(int64_t)tcp_listen(port);
            break;
        }
        case 6: { /* accept(listen_conn) → conn */
            int lc = (int)arg1;
            regs->rax = (uint64_t)(int64_t)tcp_accept(lc);
            break;
        }
        case 7: { /* resolve(hostname, ip_out) → 0 or -1 */
            const char *hostname = (const char *)arg1;
            uint32_t *ip_out = (uint32_t *)arg2;
            regs->rax = (uint64_t)(int64_t)dns_resolve(hostname, ip_out);
            break;
        }
        case 8: { /* udp_bind(port) → sock index or -1 */
            uint16_t port = (uint16_t)arg1;
            regs->rax = (uint64_t)(int64_t)udp_bind(port);
            break;
        }
        case 9: { /* udp_sendto(sock, dst_ip, dst_port, buf, len) */
            /* Pack: arg1 = sock|(dst_port<<32), arg2 = dst_ip|(len<<32) */
            /* Extra ptr passed via rdx reuse — we need 5 params but only have 3 args */
            /* Use: arg0=9, arg1=sock|(dst_port<<16)|(len<<32), arg2=dst_ip */
            /* buf pointer stored in per-task scratch by userspace before syscall */
            /* Simpler: arg1 = sock, arg2 = pointer to struct { ip, port, buf, len } */
            uint64_t *params = (uint64_t *)arg1;
            int sock = (int)params[0];
            uint32_t dst_ip = (uint32_t)params[1];
            uint16_t dst_port = (uint16_t)params[2];
            const void *buf = (const void *)params[3];
            uint16_t len = (uint16_t)params[4];
            regs->rax = (uint64_t)(int64_t)udp_sendto(sock, dst_ip, dst_port, buf, len);
            break;
        }
        case 10: { /* udp_recvfrom(sock, buf, len, src_ip_out, src_port_out) */
            uint64_t *params = (uint64_t *)arg1;
            int sock = (int)params[0];
            void *buf = (void *)params[1];
            uint16_t len = (uint16_t)params[2];
            uint32_t *src_ip = (uint32_t *)params[3];
            uint16_t *src_port = (uint16_t *)params[4];
            regs->rax = (uint64_t)(int64_t)udp_recvfrom(sock, buf, len, src_ip, src_port);
            break;
        }
        case 11: { /* udp_close(sock) */
            udp_sock_close((int)arg1);
            regs->rax = 0;
            break;
        }
        default:
            regs->rax = (uint64_t)-1;
            break;
        }
        break;
    }
    case SYS_UNLINK: {
        const char *path = (const char *)arg0;
        regs->rax = (uint64_t)vfs_unlink(path);
        break;
    }
    case SYS_GETUID: {
        struct task *t = task_current();
        regs->rax = ((uint64_t)t->uid << 16) | (uint64_t)t->gid;
        break;
    }
    case SYS_SETUID: {
        struct task *t = task_current();
        uint16_t new_uid = (uint16_t)arg0;
        uint16_t new_gid = (uint16_t)arg1;
        /* Only root can change to any uid; non-root can only set own uid */
        if (t->uid == 0 || new_uid == t->uid) {
            t->uid = new_uid;
            t->gid = new_gid;
            regs->rax = 0;
        } else {
            regs->rax = (uint64_t)-1;
        }
        break;
    }
    default:
        regs->rax = (uint64_t)-1;
        break;
    }
}

void syscall_init(void) {
    idt_set_gate(0x80, (uint64_t)syscall_stub, 0x08, 0xEE);
    kprintf("[SYSCALL] Initialized (INT 0x80)\n");
}
