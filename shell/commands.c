#include "shell/commands.h"
#include "drivers/vga.h"
#include "drivers/pit.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "cpu/ports.h"
#include "lib/printf.h"
#include "lib/string.h"
#include "lib/util.h"
#include "task/task.h"
#include "task/signal.h"
#include <plantos/signal.h>
#include "fs/vfs.h"
#include "fs/pipe.h"
#include "fs/fat.h"
#include "fs/elf_loader.h"
#include "mm/vmm.h"
#include "drivers/ata.h"
#include "drivers/pci.h"
#include "drivers/fb.h"
#include "drivers/mouse.h"
#include "drivers/keyboard.h"
#include "fs/bcache.h"
#include "gui/wm.h"
#include "gui/terminal.h"
#include "kernel/env.h"
#include "kernel/console.h"
#include "shell/editor.h"
#include "shell/shell.h"
#include "cpu/spinlock.h"
#include "cpu/apic.h"
#include "net/net.h"
#include "net/icmp.h"
#include "net/arp.h"
#include "net/tcp.h"
#include "net/dns.h"
#include "drivers/e1000.h"

/* User-mode demo entry point */
extern void user_demo_entry(void);

typedef void (*cmd_func_t)(int argc, char **argv);

struct command {
    const char *name;
    const char *description;
    cmd_func_t func;
};

static void cmd_help(int argc, char **argv);
static void cmd_clear(int argc, char **argv);
static void cmd_echo(int argc, char **argv);
static void cmd_meminfo(int argc, char **argv);
static void cmd_ticks(int argc, char **argv);
static void cmd_reboot(int argc, char **argv);
static void cmd_heap(int argc, char **argv);
static void cmd_about(int argc, char **argv);
static void cmd_ps(int argc, char **argv);
static void cmd_kill(int argc, char **argv);
static void cmd_ls(int argc, char **argv);
static void cmd_cat(int argc, char **argv);
static void cmd_touch(int argc, char **argv);
static void cmd_mkdir(int argc, char **argv);
static void cmd_write(int argc, char **argv);
static void cmd_spawn(int argc, char **argv);
static void cmd_exec(int argc, char **argv);
static void cmd_pipe(int argc, char **argv);
static void cmd_lspci(int argc, char **argv);
static void cmd_diskinfo(int argc, char **argv);
static void cmd_mount(int argc, char **argv);
static void cmd_umount(int argc, char **argv);
static void cmd_sync(int argc, char **argv);
static void cmd_cache(int argc, char **argv);
static void cmd_gfxtest(int argc, char **argv);
static void cmd_desktop(int argc, char **argv);
static void cmd_mousetest(int argc, char **argv);
static void cmd_setenv(int argc, char **argv);
static void cmd_getenv(int argc, char **argv);
static void cmd_env(int argc, char **argv);
static void cmd_unset(int argc, char **argv);
static void cmd_cd(int argc, char **argv);
static void cmd_pwd(int argc, char **argv);
static void cmd_exit(int argc, char **argv);
static void cmd_jobs(int argc, char **argv);
static void cmd_edit(int argc, char **argv);
static void cmd_netinfo(int argc, char **argv);
static void cmd_ping(int argc, char **argv);
static void cmd_tcptest(int argc, char **argv);
static void cmd_resolve(int argc, char **argv);
static void cmd_http(int argc, char **argv);
static void cmd_run(int argc, char **argv);
static void cmd_kthread(int argc, char **argv);
static void cmd_rm(int argc, char **argv);
static void cmd_fg(int argc, char **argv);
static void cmd_bg(int argc, char **argv);
static void cmd_whoami(int argc, char **argv);
static void cmd_id(int argc, char **argv);
static void cmd_su(int argc, char **argv);
static void cmd_chmod(int argc, char **argv);
static void cmd_cpuinfo(int argc, char **argv);
static void cmd_true(int argc, char **argv);
static void cmd_false(int argc, char **argv);
static void cmd_test(int argc, char **argv);
static void cmd_export(int argc, char **argv);
static void cmd_set(int argc, char **argv);
static void cmd_expr(int argc, char **argv);
static int resolve_or_parse_ip(const char *host, uint32_t *ip_out);
static void job_add(uint64_t pid, const char *name);
static const char *uid_to_name(uint16_t uid);

static struct command commands[] = {
    {"help",    "Show available commands",      cmd_help},
    {"clear",   "Clear the screen",             cmd_clear},
    {"echo",    "Print text to the screen",     cmd_echo},
    {"meminfo", "Show memory information",      cmd_meminfo},
    {"heap",    "Show heap statistics",         cmd_heap},
    {"ticks",   "Show timer tick count",        cmd_ticks},
    {"ps",      "List running tasks",           cmd_ps},
    {"kill",    "Kill a task by PID",           cmd_kill},
    {"ls",      "List directory contents",      cmd_ls},
    {"cat",     "Display file contents",        cmd_cat},
    {"touch",   "Create an empty file",         cmd_touch},
    {"mkdir",   "Create a directory",           cmd_mkdir},
    {"write",   "Write text to a file",         cmd_write},
    {"spawn",   "Launch user-mode demo task",   cmd_spawn},
    {"exec",    "Run an ELF binary from ramfs", cmd_exec},
    {"pipe",    "Demo pipe IPC",                cmd_pipe},
    {"lspci",   "List PCI devices",             cmd_lspci},
    {"diskinfo","Show disk information",        cmd_diskinfo},
    {"mount",   "Mount FAT32 disk partition",   cmd_mount},
    {"umount",  "Unmount FAT32 filesystem",     cmd_umount},
    {"sync",    "Flush disk cache to disk",     cmd_sync},
    {"cache",   "Show buffer cache stats",      cmd_cache},
    {"gfxtest", "Test framebuffer graphics",    cmd_gfxtest},
    {"desktop", "Enter graphical desktop",      cmd_desktop},
    {"mousetest","Debug PS/2 mouse input",      cmd_mousetest},
    {"cd",      "Change directory",              cmd_cd},
    {"pwd",     "Print working directory",      cmd_pwd},
    {"setenv",  "Set environment variable",     cmd_setenv},
    {"getenv",  "Get environment variable",     cmd_getenv},
    {"env",     "List environment variables",   cmd_env},
    {"unset",   "Unset environment variable",   cmd_unset},
    {"edit",    "Edit a file (nano-like)",      cmd_edit},
    {"netinfo", "Show network information",     cmd_netinfo},
    {"ping",    "Ping an IP address",           cmd_ping},
    {"tcptest", "Test TCP connection",          cmd_tcptest},
    {"resolve", "Resolve hostname via DNS",    cmd_resolve},
    {"http",    "HTTP GET request",            cmd_http},
    {"run",     "Run a shell script file",     cmd_run},
    {"kthread", "Demo kernel threads + spinlock", cmd_kthread},
    {"rm",      "Remove a file",                cmd_rm},
    {"fg",      "Resume stopped job in foreground", cmd_fg},
    {"bg",      "Resume stopped job in background", cmd_bg},
    {"whoami",  "Print current user name",      cmd_whoami},
    {"id",      "Print user and group IDs",     cmd_id},
    {"su",      "Switch user (su <username>)",  cmd_su},
    {"chmod",   "Change file permissions",      cmd_chmod},
    {"cpuinfo", "Show CPU information",         cmd_cpuinfo},
    {"true",    "Return success (exit 0)",      cmd_true},
    {"false",   "Return failure (exit 1)",      cmd_false},
    {"test",    "Evaluate conditional expression", cmd_test},
    {"[",       "Evaluate conditional expression", cmd_test},
    {"export",  "Export shell var to environment", cmd_export},
    {"set",     "List shell variables",          cmd_set},
    {"expr",    "Evaluate arithmetic expression", cmd_expr},
    {"jobs",    "List background tasks",        cmd_jobs},
    {"exit",    "Exit the shell",               cmd_exit},
    {"reboot",  "Reboot the system",            cmd_reboot},
    {"about",   "About PlantOS",                cmd_about},
    {NULL, NULL, NULL}
};

static void cmd_help(int argc, char **argv) {
    (void)argc; (void)argv;
    kprintf("Available commands:\n");
    for (int i = 0; commands[i].name; i++) {
        vga_set_color(VGA_YELLOW, VGA_BLACK);
        kprintf("  %s", commands[i].name);
        int pad = 10 - (int)strlen(commands[i].name);
        while (pad-- > 0) kprintf(" ");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        kprintf("  %s\n", commands[i].description);
    }
}

static void cmd_clear(int argc, char **argv) {
    (void)argc; (void)argv;
    vga_clear();
}

static void cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) kprintf(" ");
        kprintf("%s", argv[i]);
    }
    kprintf("\n");
}

static void cmd_meminfo(int argc, char **argv) {
    (void)argc; (void)argv;
    uint64_t total = pmm_get_total_pages();
    uint64_t used  = pmm_get_used_pages();
    uint64_t free  = pmm_get_free_pages();
    kprintf("Physical Memory:\n");
    kprintf("  Total: %llu MB (%llu pages)\n", (total * 4096) / (1024 * 1024), total);
    kprintf("  Used:  %llu MB (%llu pages)\n", (used * 4096) / (1024 * 1024), used);
    kprintf("  Free:  %llu MB (%llu pages)\n", (free * 4096) / (1024 * 1024), free);
}

static void cmd_heap(int argc, char **argv) {
    (void)argc; (void)argv;
    heap_dump_stats();
}

static void cmd_ticks(int argc, char **argv) {
    (void)argc; (void)argv;
    uint64_t t = pit_get_ticks();
    kprintf("Timer ticks: %llu (uptime: %llu seconds)\n", t, t / PIT_FREQ);
}

static void cmd_reboot(int argc, char **argv) {
    (void)argc; (void)argv;
    kprintf("Rebooting...\n");
    uint8_t good = 0x02;
    while (good & 0x02)
        good = inb(0x64);
    outb(0x64, 0xFE);
    __asm__ volatile ("hlt");
}

static void cmd_about(int argc, char **argv) {
    (void)argc; (void)argv;
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    kprintf("PlantOS v0.9\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    kprintf("A minimal x86_64 operating system\n");
    kprintf("Built from scratch with C and Assembly\n");
}

static const char *state_str(task_state_t s) {
    switch (s) {
        case TASK_READY:   return "READY";
        case TASK_RUNNING: return "RUNNING";
        case TASK_ZOMBIE:  return "ZOMBIE";
        case TASK_BLOCKED: return "BLOCKED";
        case TASK_STOPPED: return "STOPPED";
        default:           return "???";
    }
}

static void pad_right(const char *s, int width) {
    int len = (int)strlen(s);
    kprintf("%s", s);
    for (int i = len; i < width; i++)
        kprintf(" ");
}

static void cmd_ps(int argc, char **argv) {
    (void)argc; (void)argv;
    kprintf("PID  PGID UID  NAME                 STATE    TICKS\n");
    kprintf("---  ---- ---  ----                 -----    -----\n");

    struct task *head = task_get_list();
    if (!head) return;

    struct task *t = head;
    do {
        if (t->state != TASK_UNUSED) {
            char pidbuf[12];
            utoa(t->pid, pidbuf, 10);
            pad_right(pidbuf, 5);
            char pgidbuf[12];
            utoa(t->pgid, pgidbuf, 10);
            pad_right(pgidbuf, 5);
            char uidbuf[12];
            utoa(t->uid, uidbuf, 10);
            pad_right(uidbuf, 5);
            pad_right(t->name, 21);
            pad_right(state_str(t->state), 9);
            char tickbuf[20];
            utoa(t->ticks, tickbuf, 10);
            kprintf("%s\n", tickbuf);
        }
        t = t->next;
    } while (t != head);
}

static uint64_t parse_num(const char *s) {
    uint64_t n = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9') return (uint64_t)-1;
        n = n * 10 + (s[i] - '0');
    }
    return n;
}

static void cmd_kill(int argc, char **argv) {
    if (argc < 2) {
        kprintf("Usage: kill <pid> [signal]\n");
        kprintf("  Signals: 1=SIGKILL 2=SIGTERM 3=SIGUSR1 4=SIGUSR2 5=SIGPIPE\n");
        return;
    }
    uint64_t pid = parse_num(argv[1]);
    if (pid == (uint64_t)-1) {
        kprintf("Invalid PID: %s\n", argv[1]);
        return;
    }

    int signum = SIGKILL; /* Default */
    if (argc >= 3) {
        uint64_t s = parse_num(argv[2]);
        if (s == (uint64_t)-1 || s < 1 || s >= MAX_SIGNALS) {
            kprintf("Invalid signal: %s\n", argv[2]);
            return;
        }
        signum = (int)s;
    }

    struct task *t = task_find(pid);
    if (!t) {
        kprintf("No task with pid %llu\n", pid);
        return;
    }

    if (t->is_user) {
        /* Send signal to user task */
        if (signal_send(pid, signum) == 0)
            kprintf("Sent signal %d to pid %llu\n", signum, pid);
        else
            kprintf("Failed to send signal\n");
    } else {
        /* Kernel task: use hard kill */
        task_kill(pid);
    }
}

static void format_mode(uint16_t mode, char *buf) {
    /* drwxrwxrwx */
    const char *rwx = "rwx";
    for (int i = 0; i < 3; i++) {
        int bits = (mode >> (6 - i * 3)) & 7;
        buf[i * 3 + 0] = (bits & 4) ? rwx[0] : '-';
        buf[i * 3 + 1] = (bits & 2) ? rwx[1] : '-';
        buf[i * 3 + 2] = (bits & 1) ? rwx[2] : '-';
    }
    buf[9] = '\0';
}

static void cmd_ls(int argc, char **argv) {
    bool long_fmt = false;
    const char *dir_arg = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0) long_fmt = true;
        else dir_arg = argv[i];
    }

    char resolved[VFS_PATH_MAX];
    if (dir_arg)
        path_resolve(dir_arg, resolved, sizeof(resolved));
    else
        path_resolve(".", resolved, sizeof(resolved));
    const char *path = resolved;
    int fd = vfs_open(path, 0);
    if (fd < 0) {
        kprintf("ls: cannot access '%s': no such directory\n", path);
        return;
    }

    struct vfs_dirent de;
    while (vfs_readdir(fd, &de) == 0) {
        struct vfs_stat st;
        /* Build full path for stat */
        char fullpath[128];
        int plen = strlen(path);
        memcpy(fullpath, path, plen);
        if (plen > 1) fullpath[plen++] = '/';
        else if (path[0] == '/' && plen == 1) { /* root */ }
        strcpy(fullpath + plen, de.name);

        if (vfs_stat(fullpath, &st) == 0) {
            if (long_fmt) {
                char mbuf[10];
                format_mode(st.mode, mbuf);
                char ownbuf[16], grpbuf[16];
                {
                    const char *o = uid_to_name(st.uid);
                    int oi = 0;
                    while (o[oi] && oi < 15) { ownbuf[oi] = o[oi]; oi++; }
                    ownbuf[oi] = '\0';
                }
                {
                    const char *g = uid_to_name(st.gid);
                    int gi = 0;
                    while (g[gi] && gi < 15) { grpbuf[gi] = g[gi]; gi++; }
                    grpbuf[gi] = '\0';
                }
                kprintf("%c%s %s %s ",
                    st.type == VFS_DIR ? 'd' : '-',
                    mbuf, ownbuf, grpbuf);
                if (st.type == VFS_DIR) {
                    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
                    kprintf("%s/\n", de.name);
                    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
                } else {
                    kprintf("%7llu %s\n", (uint64_t)st.size, de.name);
                }
            } else {
                if (st.type == VFS_DIR) {
                    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
                    kprintf("  %s/\n", de.name);
                    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
                } else {
                    kprintf("  %s  (%llu bytes)\n", de.name, (uint64_t)st.size);
                }
            }
        } else {
            kprintf("  %s\n", de.name);
        }
    }

    vfs_close(fd);
}

static void cmd_cat(int argc, char **argv) {
    if (argc < 2) {
        kprintf("Usage: cat <file>\n");
        return;
    }
    char resolved[VFS_PATH_MAX];
    path_resolve(argv[1], resolved, sizeof(resolved));
    int fd = vfs_open(resolved, 0);
    if (fd < 0) {
        kprintf("cat: %s: no such file\n", argv[1]);
        return;
    }
    char buf[512];
    int n;
    while ((n = vfs_read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        kprintf("%s", buf);
    }
    kprintf("\n");
    vfs_close(fd);
}

static void cmd_touch(int argc, char **argv) {
    if (argc < 2) {
        kprintf("Usage: touch <file>\n");
        return;
    }
    char resolved[VFS_PATH_MAX];
    path_resolve(argv[1], resolved, sizeof(resolved));
    int fd = vfs_open(resolved, VFS_O_CREATE);
    if (fd < 0) {
        kprintf("touch: cannot create '%s'\n", argv[1]);
        return;
    }
    vfs_close(fd);
}

static void cmd_mkdir(int argc, char **argv) {
    if (argc < 2) {
        kprintf("Usage: mkdir <dir>\n");
        return;
    }
    char resolved[VFS_PATH_MAX];
    path_resolve(argv[1], resolved, sizeof(resolved));
    if (vfs_mkdir(resolved) < 0) {
        kprintf("mkdir: cannot create '%s'\n", argv[1]);
    }
}

static void cmd_write(int argc, char **argv) {
    if (argc < 3) {
        kprintf("Usage: write <file> <text...>\n");
        return;
    }
    char resolved[VFS_PATH_MAX];
    path_resolve(argv[1], resolved, sizeof(resolved));
    int fd = vfs_open(resolved, VFS_O_CREATE);
    if (fd < 0) {
        kprintf("write: cannot open '%s'\n", argv[1]);
        return;
    }
    /* Concatenate remaining args with spaces */
    for (int i = 2; i < argc; i++) {
        vfs_write(fd, argv[i], strlen(argv[i]));
        if (i < argc - 1)
            vfs_write(fd, " ", 1);
    }
    vfs_close(fd);
}

static void cmd_spawn(int argc, char **argv) {
    (void)argc; (void)argv;
    struct task *t = task_create_user("user_demo", user_demo_entry);
    if (t) {
        kprintf("Spawned user-mode task (pid %llu)\n", t->pid);
    } else {
        kprintf("Failed to spawn user task\n");
    }
}

static void cmd_exec(int argc, char **argv) {
    if (argc < 2) {
        kprintf("Usage: exec <program> [args...] [&]\n");
        return;
    }

    /* Check for background '&' suffix */
    bool background = false;
    if (argc >= 2 && strcmp(argv[argc - 1], "&") == 0) {
        background = true;
        argc--;
    }

    /* Resolve path using PATH lookup */
    const char *path = argv[1];
    char pathbuf[VFS_PATH_MAX];
    if (path[0] != '/') {
        if (path_lookup(path, pathbuf, sizeof(pathbuf)) == 0) {
            path = pathbuf;
        } else {
            kprintf("exec: command not found: %s\n", path);
            return;
        }
    }
    /* Create per-process address space */
    uint64_t new_cr3 = vmm_create_address_space();
    if (!new_cr3) {
        kprintf("exec: failed to create address space\n");
        return;
    }
    struct elf_info info;
    if (elf_load(path, &info, new_cr3) < 0) {
        kprintf("exec: failed to load '%s'\n", path);
        vmm_destroy_address_space(new_cr3);
        return;
    }
    /* Pass remaining argv (argv[1..]) as program arguments */
    int prog_argc = argc - 1;
    char **prog_argv = &argv[1];
    struct task *t = task_create_user_elf(path, info.entry,
                                          info.load_base, info.num_pages,
                                          new_cr3, prog_argc, prog_argv);
    if (!t) {
        kprintf("exec: failed to create task\n");
        vmm_destroy_address_space(new_cr3);
        return;
    }

    if (background) {
        kprintf("[%llu] %s\n", t->pid, path);
        job_add(t->pid, path);
    } else {
        /* Foreground: give it the keyboard and wait */
        key_handler_t saved = keyboard_get_handler();
        console_set_fg(t->pid);
        keyboard_set_handler(console_key_handler);

        /* Wait for task to finish or stop */
        int ret = task_waitpid(t->pid);

        /* Restore shell */
        console_clear_fg();
        keyboard_set_handler(saved);

        if (ret == -2) {
            /* Task was stopped (Ctrl+Z) — move to job table */
            kprintf("\n[+] Stopped: %s (pid %llu)\n", path, t->pid);
            job_add(t->pid, path);
        }
    }
}

static struct {
    int read_fd;
    int write_fd;
} pipe_demo_ctx;

static void pipe_writer_task(void) {
    const char *msg = "Hello through pipe!";
    int len = 0;
    while (msg[len]) len++;

    pipe_fd_write(pipe_demo_ctx.write_fd, msg, (size_t)len);
    kprintf("[PIPE-WRITER] Wrote %d bytes\n", len);

    pipe_fd_close(pipe_demo_ctx.write_fd);
    kprintf("[PIPE-WRITER] Closed write end\n");
    task_exit();
}

static void pipe_reader_task(void) {
    char buf[128];
    int n = pipe_fd_read(pipe_demo_ctx.read_fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        kprintf("[PIPE-READER] Read %d bytes: \"%s\"\n", n, buf);
    } else {
        kprintf("[PIPE-READER] Read returned %d\n", n);
    }

    pipe_fd_close(pipe_demo_ctx.read_fd);
    kprintf("[PIPE-READER] Closed read end\n");
    task_exit();
}

static void cmd_pipe(int argc, char **argv) {
    (void)argc; (void)argv;

    int read_fd, write_fd;
    if (pipe_create(&read_fd, &write_fd) < 0) {
        kprintf("Failed to create pipe\n");
        return;
    }
    kprintf("Pipe created: read_fd=%d, write_fd=%d\n", read_fd, write_fd);

    pipe_demo_ctx.read_fd = read_fd;
    pipe_demo_ctx.write_fd = write_fd;

    struct task *writer = task_create("pipe-writer", pipe_writer_task);
    struct task *reader = task_create("pipe-reader", pipe_reader_task);
    if (writer && reader) {
        kprintf("Spawned pipe-writer (pid %llu) and pipe-reader (pid %llu)\n",
                writer->pid, reader->pid);
    } else {
        kprintf("Failed to spawn pipe tasks\n");
    }
}

static void cmd_lspci(int argc, char **argv) {
    (void)argc; (void)argv;
    int count = pci_get_device_count();
    if (count == 0) {
        kprintf("No PCI devices found\n");
        return;
    }
    kprintf("BUS:SL.FN  VENDOR:DEVICE  CLASS\n");
    for (int i = 0; i < count; i++) {
        struct pci_device *d = pci_get_device(i);
        kprintf("%02x:%02x.%d    %04x:%04x      %02x:%02x\n",
                d->bus, d->slot, d->func,
                d->vendor_id, d->device_id,
                d->class_code, d->subclass);
    }
}

static void cmd_diskinfo(int argc, char **argv) {
    (void)argc; (void)argv;
    if (!ata_is_present()) {
        kprintf("No ATA disk detected\n");
        return;
    }
    uint32_t sectors = ata_get_sector_count();
    kprintf("ATA Primary Master:\n");
    kprintf("  Sectors: %u\n", sectors);
    kprintf("  Size:    %u MB\n", sectors / 2048);

    if (vfs_fat_mounted()) {
        kprintf("FAT32 mounted at: %s\n", vfs_fat_mount_point());
    } else {
        kprintf("No filesystem mounted\n");
    }
}

static void cmd_mount(int argc, char **argv) {
    (void)argc; (void)argv;
    if (!ata_is_present()) {
        kprintf("No disk available\n");
        return;
    }
    if (vfs_fat_mounted()) {
        kprintf("Already mounted at %s\n", vfs_fat_mount_point());
        return;
    }
    fat_init();
    if (fat_mount_first_partition() == 0) {
        const char *mpoint = (argc > 1) ? argv[1] : "/disk";
        vfs_mount_fat(mpoint);
    } else {
        kprintf("Failed to mount FAT32 partition\n");
    }
}

static void cmd_umount(int argc, char **argv) {
    (void)argc; (void)argv;
    if (!vfs_fat_mounted()) {
        kprintf("Nothing mounted\n");
        return;
    }
    vfs_unmount_fat();
    kprintf("Unmounted\n");
}

static void cmd_sync(int argc, char **argv) {
    (void)argc; (void)argv;
    int flushed = bcache_sync();
    if (flushed < 0) {
        kprintf("sync: error flushing cache\n");
    } else {
        kprintf("Flushed %d dirty buffers to disk\n", flushed);
    }
}

static void cmd_cache(int argc, char **argv) {
    (void)argc; (void)argv;
    kprintf("Buffer cache: %d slots x %d bytes = %d KB\n",
            BCACHE_SIZE, SECTOR_SIZE, (BCACHE_SIZE * SECTOR_SIZE) / 1024);
    kprintf("Hits: %u, Misses: %u", bcache_get_hits(), bcache_get_misses());
    uint32_t total = bcache_get_hits() + bcache_get_misses();
    if (total > 0) {
        kprintf(" (hit rate: %u%%)", (bcache_get_hits() * 100) / total);
    }
    kprintf("\n");
}

/* ---- Environment variable commands ---- */

static void cmd_setenv(int argc, char **argv) {
    if (argc < 3) {
        kprintf("Usage: setenv <key> <value>\n");
        return;
    }
    if (env_set(argv[1], argv[2]) < 0)
        kprintf("setenv: failed (table full or name too long)\n");
}

static void cmd_getenv(int argc, char **argv) {
    if (argc < 2) {
        kprintf("Usage: getenv <key>\n");
        return;
    }
    const char *val = env_get(argv[1]);
    if (val)
        kprintf("%s=%s\n", argv[1], val);
    else
        kprintf("%s: not set\n", argv[1]);
}

static void cmd_env(int argc, char **argv) {
    (void)argc; (void)argv;
    int n = env_count();
    for (int i = 0; i < n; i++) {
        const char *k = env_key_at(i);
        const char *v = env_val_at(i);
        if (k) kprintf("%s=%s\n", k, v);
    }
}

static void cmd_unset(int argc, char **argv) {
    if (argc < 2) {
        kprintf("Usage: unset <key>\n");
        return;
    }
    if (env_unset(argv[1]) < 0)
        kprintf("unset: '%s' not found\n", argv[1]);
}

/* ---- Directory commands ---- */

static void cmd_cd(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "/";
    if (cwd_set(path) < 0)
        kprintf("cd: no such directory: %s\n", path);
}

static void cmd_pwd(int argc, char **argv) {
    (void)argc; (void)argv;
    kprintf("%s\n", cwd_get());
}

/* ---- Job tracking ---- */

#define MAX_JOBS 16

struct job {
    uint64_t pid;
    char     name[32];
    bool     active;
};

static struct job job_table[MAX_JOBS];

static void job_add(uint64_t pid, const char *name) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!job_table[i].active) {
            job_table[i].pid = pid;
            job_table[i].active = true;
            strncpy(job_table[i].name, name, 31);
            job_table[i].name[31] = '\0';
            return;
        }
    }
}

void jobs_update(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!job_table[i].active) continue;
        struct task *t = task_find(job_table[i].pid);
        if (!t || t->state == TASK_ZOMBIE || t->state == TASK_UNUSED) {
            kprintf("[%d] Done: %s (pid %llu)\n", i + 1,
                    job_table[i].name, job_table[i].pid);
            job_table[i].active = false;
        }
    }
}

static void cmd_jobs(int argc, char **argv) {
    (void)argc; (void)argv;
    bool any = false;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!job_table[i].active) continue;
        struct task *t = task_find(job_table[i].pid);
        const char *st = "UNKNOWN";
        if (t) {
            switch (t->state) {
            case TASK_RUNNING: st = "Running"; break;
            case TASK_READY:   st = "Running"; break;
            case TASK_BLOCKED: st = "Running"; break;
            case TASK_STOPPED: st = "Stopped"; break;
            case TASK_ZOMBIE:  st = "Done"; break;
            default:           st = "Unknown"; break;
            }
        }
        kprintf("[%d] pid %llu  %s  %s\n", i + 1, job_table[i].pid, st,
                job_table[i].name);
        any = true;
    }
    if (!any)
        kprintf("No background jobs\n");
}

/* Find job by number (1-based) or most recent if no arg */
static int find_job(int argc, char **argv) {
    if (argc >= 2) {
        /* Parse %N or just N */
        const char *s = argv[1];
        if (*s == '%') s++;
        int n = 0;
        for (int i = 0; s[i]; i++) {
            if (s[i] < '0' || s[i] > '9') return -1;
            n = n * 10 + (s[i] - '0');
        }
        if (n < 1 || n > MAX_JOBS) return -1;
        return n - 1; /* Convert to 0-based index */
    }
    /* No arg: find most recent active job */
    for (int i = MAX_JOBS - 1; i >= 0; i--) {
        if (job_table[i].active) return i;
    }
    return -1;
}

static void cmd_fg(int argc, char **argv) {
    int idx = find_job(argc, argv);
    if (idx < 0 || !job_table[idx].active) {
        kprintf("fg: no such job\n");
        return;
    }
    struct task *t = task_find(job_table[idx].pid);
    if (!t) {
        kprintf("fg: job has exited\n");
        job_table[idx].active = false;
        return;
    }

    kprintf("%s\n", job_table[idx].name);

    /* Send SIGCONT if stopped */
    if (t->state == TASK_STOPPED)
        signal_send(t->pid, SIGCONT);

    /* Give it the foreground and wait */
    key_handler_t saved = keyboard_get_handler();
    console_set_fg(t->pid);
    keyboard_set_handler(console_key_handler);

    int ret = task_waitpid(t->pid);

    console_clear_fg();
    keyboard_set_handler(saved);

    if (ret == -2) {
        /* Stopped again */
        kprintf("\n[+] Stopped: %s (pid %llu)\n", job_table[idx].name, t->pid);
    } else {
        /* Completed — remove from job table */
        job_table[idx].active = false;
    }
}

static void cmd_bg(int argc, char **argv) {
    int idx = find_job(argc, argv);
    if (idx < 0 || !job_table[idx].active) {
        kprintf("bg: no such job\n");
        return;
    }
    struct task *t = task_find(job_table[idx].pid);
    if (!t) {
        kprintf("bg: job has exited\n");
        job_table[idx].active = false;
        return;
    }
    if (t->state != TASK_STOPPED) {
        kprintf("bg: job is not stopped\n");
        return;
    }
    signal_send(t->pid, SIGCONT);
    kprintf("[%d] %s &\n", idx + 1, job_table[idx].name);
}

/* ---- Exit command ---- */

static void cmd_exit(int argc, char **argv) {
    (void)argc; (void)argv;
    kprintf("Goodbye.\n");
    task_exit();
}

static void cmd_edit(int argc, char **argv) {
    if (argc < 2) {
        kprintf("Usage: edit <filename>\n");
        return;
    }
    editor_open(argv[1]);
}

static void cmd_netinfo(int argc, char **argv) {
    (void)argc; (void)argv;
    if (!net_cfg.up) {
        kprintf("Network: down\n");
        return;
    }
    kprintf("Link:    %s\n", e1000_is_link_up() ? "up" : "down");
    kprintf("MAC:     %02x:%02x:%02x:%02x:%02x:%02x\n",
            net_cfg.mac[0], net_cfg.mac[1], net_cfg.mac[2],
            net_cfg.mac[3], net_cfg.mac[4], net_cfg.mac[5]);
    kprintf("IP:      %d.%d.%d.%d\n",
            (net_cfg.ip >> 24) & 0xFF, (net_cfg.ip >> 16) & 0xFF,
            (net_cfg.ip >> 8) & 0xFF, net_cfg.ip & 0xFF);
    kprintf("Netmask: %d.%d.%d.%d\n",
            (net_cfg.netmask >> 24) & 0xFF, (net_cfg.netmask >> 16) & 0xFF,
            (net_cfg.netmask >> 8) & 0xFF, net_cfg.netmask & 0xFF);
    kprintf("Gateway: %d.%d.%d.%d\n",
            (net_cfg.gateway >> 24) & 0xFF, (net_cfg.gateway >> 16) & 0xFF,
            (net_cfg.gateway >> 8) & 0xFF, net_cfg.gateway & 0xFF);
}


static void cmd_ping(int argc, char **argv) {
    if (argc < 2) {
        kprintf("Usage: ping <host>\n");
        return;
    }
    if (!net_cfg.up) {
        kprintf("Network is down\n");
        return;
    }

    uint32_t target;
    if (resolve_or_parse_ip(argv[1], &target) < 0) {
        kprintf("Cannot resolve '%s'\n", argv[1]);
        return;
    }
    kprintf("PING %d.%d.%d.%d\n",
            (target >> 24) & 0xFF, (target >> 16) & 0xFF,
            (target >> 8) & 0xFF, target & 0xFF);

    /* First, send an ARP request to resolve the target (or gateway) */
    uint32_t next_hop = target;
    if ((target & net_cfg.netmask) != (net_cfg.ip & net_cfg.netmask))
        next_hop = net_cfg.gateway;

    uint8_t mac[6];
    if (arp_resolve(next_hop, mac) < 0) {
        arp_request(next_hop);
        /* Wait briefly for ARP reply */
        for (volatile int i = 0; i < 2000000; i++);
    }

    for (int i = 0; i < 4; i++) {
        int rtt = icmp_ping(target, (uint16_t)(i + 1), 3000);
        if (rtt >= 0) {
            kprintf("Reply from %d.%d.%d.%d: seq=%d time=%dms\n",
                    (target >> 24) & 0xFF, (target >> 16) & 0xFF,
                    (target >> 8) & 0xFF, target & 0xFF,
                    i + 1, rtt);
        } else {
            kprintf("Request timed out: seq=%d\n", i + 1);
        }
        /* Brief delay between pings */
        for (volatile int j = 0; j < 3000000; j++);
    }
}

/* Helper: resolve hostname or parse IP */
static int resolve_or_parse_ip(const char *host, uint32_t *ip_out) {
    return dns_resolve(host, ip_out);
}

static void cmd_resolve(int argc, char **argv) {
    if (argc < 2) {
        kprintf("Usage: resolve <hostname>\n");
        return;
    }
    if (!net_cfg.up) {
        kprintf("Network is down\n");
        return;
    }
    uint32_t ip;
    if (dns_resolve(argv[1], &ip) == 0) {
        kprintf("%s -> %d.%d.%d.%d\n", argv[1],
                (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
                (ip >> 8) & 0xFF, ip & 0xFF);
    } else {
        kprintf("Failed to resolve '%s'\n", argv[1]);
    }
}

static void cmd_http(int argc, char **argv) {
    if (argc < 2) {
        kprintf("Usage: http <host> [port] [path]\n");
        return;
    }
    if (!net_cfg.up) {
        kprintf("Network is down\n");
        return;
    }

    const char *host = argv[1];
    uint16_t port = 80;
    const char *path = "/";
    if (argc >= 3) {
        port = 0;
        for (int i = 0; argv[2][i]; i++)
            port = port * 10 + (argv[2][i] - '0');
    }
    if (argc >= 4) path = argv[3];

    /* Resolve hostname */
    uint32_t ip;
    if (resolve_or_parse_ip(host, &ip) < 0) {
        kprintf("Cannot resolve '%s'\n", host);
        return;
    }
    kprintf("Resolved %s -> %d.%d.%d.%d\n", host,
            (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
            (ip >> 8) & 0xFF, ip & 0xFF);

    /* ARP resolve */
    uint32_t next_hop = ip;
    if ((ip & net_cfg.netmask) != (net_cfg.ip & net_cfg.netmask))
        next_hop = net_cfg.gateway;
    uint8_t mac[6];
    if (arp_resolve(next_hop, mac) < 0) {
        arp_request(next_hop);
        uint64_t start = pit_get_ticks();
        while (arp_resolve(next_hop, mac) < 0) {
            if (pit_get_ticks() - start > 300) {
                kprintf("ARP resolution failed\n");
                return;
            }
            __asm__ volatile ("sti; hlt");
        }
    }

    /* TCP connect */
    kprintf("Connecting to %s:%d ...\n", host, port);
    int conn = tcp_connect(ip, port);
    if (conn < 0) {
        kprintf("Connection failed\n");
        return;
    }

    /* Build and send HTTP request */
    char req[512];
    int rlen = 0;
    const char *parts[] = {"GET ", path, " HTTP/1.0\r\nHost: ", host, "\r\nConnection: close\r\n\r\n"};
    for (int p = 0; p < 5; p++)
        for (int i = 0; parts[p][i]; i++)
            req[rlen++] = parts[p][i];
    req[rlen] = '\0';

    tcp_send(conn, req, (uint16_t)rlen);

    /* Receive and display */
    char buf[512];
    int total = 0;
    while (1) {
        int n = tcp_recv(conn, buf, sizeof(buf) - 1);
        if (n <= 0) break;
        buf[n] = '\0';
        kprintf("%s", buf);
        total += n;
        if (total > 4096) {
            kprintf("\n... (truncated)\n");
            break;
        }
    }
    kprintf("\n--- %d bytes received ---\n", total);
    tcp_close(conn);
}

static void cmd_tcptest(int argc, char **argv) {
    if (!net_cfg.up) {
        kprintf("Network is down\n");
        return;
    }

    /* Default: connect to QEMU gateway port 80 (HTTP) */
    uint32_t target = net_cfg.gateway;
    uint16_t port = 80;
    if (argc >= 2) {
        if (resolve_or_parse_ip(argv[1], &target) < 0) {
            kprintf("Cannot resolve '%s'\n", argv[1]);
            return;
        }
    }
    if (argc >= 3) {
        port = 0;
        for (int i = 0; argv[2][i]; i++)
            port = port * 10 + (argv[2][i] - '0');
    }

    kprintf("Connecting to %d.%d.%d.%d:%d ...\n",
            (target >> 24) & 0xFF, (target >> 16) & 0xFF,
            (target >> 8) & 0xFF, target & 0xFF, port);

    /* Ensure ARP is resolved first */
    uint32_t next_hop = target;
    if ((target & net_cfg.netmask) != (net_cfg.ip & net_cfg.netmask))
        next_hop = net_cfg.gateway;
    uint8_t mac[6];
    if (arp_resolve(next_hop, mac) < 0) {
        arp_request(next_hop);
        uint64_t start = pit_get_ticks();
        while (arp_resolve(next_hop, mac) < 0) {
            if (pit_get_ticks() - start > 300) {
                kprintf("ARP resolution failed\n");
                return;
            }
            __asm__ volatile ("hlt");
        }
    }

    int conn = tcp_connect(target, port);
    if (conn < 0) {
        kprintf("Connection failed\n");
        return;
    }
    kprintf("Connected! (conn %d)\n", conn);

    /* Send a simple HTTP GET request */
    const char *request = "GET / HTTP/1.0\r\nHost: 10.0.2.2\r\n\r\n";
    int sent = tcp_send(conn, request, (uint16_t)strlen(request));
    kprintf("Sent %d bytes\n", sent);

    /* Receive and display response */
    char buf[512];
    int total = 0;
    while (1) {
        int n = tcp_recv(conn, buf, sizeof(buf) - 1);
        if (n <= 0) break;
        buf[n] = '\0';
        kprintf("%s", buf);
        total += n;
        if (total > 2048) {
            kprintf("\n... (truncated)\n");
            break;
        }
    }

    kprintf("\n--- Received %d bytes total ---\n", total);
    tcp_close(conn);
    kprintf("Connection closed\n");
}

/* ---- Desktop mode ---- */

#define DESKTOP_W 640
#define DESKTOP_H 480
#define TASKBAR_H 32
#define CURSOR_W  12
#define CURSOR_H  16

/* 12x16 arrow cursor bitmap (1 = white, 2 = black outline) */
static const uint8_t cursor_bitmap[CURSOR_H][CURSOR_W] = {
    {2,0,0,0,0,0,0,0,0,0,0,0},
    {2,2,0,0,0,0,0,0,0,0,0,0},
    {2,1,2,0,0,0,0,0,0,0,0,0},
    {2,1,1,2,0,0,0,0,0,0,0,0},
    {2,1,1,1,2,0,0,0,0,0,0,0},
    {2,1,1,1,1,2,0,0,0,0,0,0},
    {2,1,1,1,1,1,2,0,0,0,0,0},
    {2,1,1,1,1,1,1,2,0,0,0,0},
    {2,1,1,1,1,1,1,1,2,0,0,0},
    {2,1,1,1,1,1,1,1,1,2,0,0},
    {2,1,1,1,1,1,2,2,2,2,2,0},
    {2,1,1,2,1,1,2,0,0,0,0,0},
    {2,1,2,0,2,1,1,2,0,0,0,0},
    {2,2,0,0,2,1,1,2,0,0,0,0},
    {2,0,0,0,0,2,1,1,2,0,0,0},
    {0,0,0,0,0,2,2,2,0,0,0,0},
};

static uint32_t saved_under[CURSOR_W * CURSOR_H];

/* Cached LFB pointer and stride for cursor drawing (set once at desktop entry) */
static volatile uint32_t *desktop_lfb;
static uint32_t desktop_stride;
static uint32_t desktop_w, desktop_h;

static void save_under_cursor(int32_t cx, int32_t cy) {
    for (int row = 0; row < CURSOR_H; row++) {
        for (int col = 0; col < CURSOR_W; col++) {
            int32_t sx = cx + col, sy = cy + row;
            if (sx >= 0 && sx < (int32_t)desktop_w && sy >= 0 && sy < (int32_t)desktop_h)
                saved_under[row * CURSOR_W + col] = desktop_lfb[sy * desktop_stride + sx];
            else
                saved_under[row * CURSOR_W + col] = 0;
        }
    }
}

static void restore_under_cursor(int32_t cx, int32_t cy) {
    for (int row = 0; row < CURSOR_H; row++) {
        for (int col = 0; col < CURSOR_W; col++) {
            int32_t sx = cx + col, sy = cy + row;
            if (sx >= 0 && sx < (int32_t)desktop_w && sy >= 0 && sy < (int32_t)desktop_h)
                desktop_lfb[sy * desktop_stride + sx] = saved_under[row * CURSOR_W + col];
        }
    }
}

static void draw_cursor(int32_t cx, int32_t cy) {
    for (int row = 0; row < CURSOR_H; row++) {
        for (int col = 0; col < CURSOR_W; col++) {
            uint8_t p = cursor_bitmap[row][col];
            if (p == 0) continue;
            int32_t sx = cx + col, sy = cy + row;
            if (sx >= 0 && sx < (int32_t)desktop_w && sy >= 0 && sy < (int32_t)desktop_h) {
                uint32_t color = (p == 1) ? FB_WHITE : FB_BLACK;
                desktop_lfb[sy * desktop_stride + sx] = color;
            }
        }
    }
}


/* Render desktop background + taskbar into a pixel buffer */
static void render_background(uint32_t *bg) {
    /* Gradient */
    for (uint32_t y = 0; y < DESKTOP_H - TASKBAR_H; y++) {
        uint8_t r = (uint8_t)(20 + y * 30 / DESKTOP_H);
        uint8_t g = (uint8_t)(60 + y * 80 / DESKTOP_H);
        uint8_t b = (uint8_t)(100 + y * 100 / DESKTOP_H);
        uint32_t color = FB_RGB(r, g, b);
        for (uint32_t x = 0; x < DESKTOP_W; x++)
            bg[y * DESKTOP_W + x] = color;
    }
    /* Taskbar area */
    for (uint32_t y = DESKTOP_H - TASKBAR_H; y < DESKTOP_H; y++)
        for (uint32_t x = 0; x < DESKTOP_W; x++)
            bg[y * DESKTOP_W + x] = FB_RGB(40, 40, 60);
    /* Taskbar separator */
    for (uint32_t x = 0; x < DESKTOP_W; x++)
        bg[(DESKTOP_H - TASKBAR_H) * DESKTOP_W + x] = FB_RGB(80, 80, 120);
}

/* Draw text into background buffer */
static void bg_draw_char(uint32_t *bg, uint32_t x, uint32_t y, char c,
                          uint32_t fg, uint32_t bg_col) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t *glyph = fb_font8x16[c - 32];
    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            uint32_t px = x + col, py = y + row;
            if (px < DESKTOP_W && py < DESKTOP_H)
                bg[py * DESKTOP_W + px] = (bits & (0x80 >> col)) ? fg : bg_col;
        }
    }
}

static void bg_draw_string(uint32_t *bg, uint32_t x, uint32_t y,
                            const char *s, uint32_t fg, uint32_t bg_col) {
    uint32_t cx = x;
    while (*s) {
        bg_draw_char(bg, cx, y, *s, fg, bg_col);
        cx += 8;
        s++;
    }
}

static void bg_fill_rect(uint32_t *bg, uint32_t x, uint32_t y,
                          uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t j = y; j < y + h && j < DESKTOP_H; j++)
        for (uint32_t i = x; i < x + w && i < DESKTOP_W; i++)
            bg[j * DESKTOP_W + i] = color;
}

static void render_background_decorations(uint32_t *bg) {
    /* Title */
    bg_draw_string(bg, DESKTOP_W / 2 - 60, 20, "PlantOS Desktop",
                   FB_WHITE, FB_RGB(20, 60, 100));
    /* Instructions */
    bg_draw_string(bg, 16, 60, "Click icons to open windows",
                   FB_RGB(200, 220, 255), FB_RGB(22, 66, 110));
    bg_draw_string(bg, 16, 80, "Drag title bars to move. ESC to exit.",
                   FB_RGB(200, 220, 255), FB_RGB(24, 72, 120));

    /* Icon 1: Terminal */
    bg_fill_rect(bg, 50, 140, 64, 64, FB_RGB(30, 30, 50));
    bg_fill_rect(bg, 52, 142, 60, 60, FB_RGB(10, 10, 30));
    bg_draw_string(bg, 56, 158, ">_", FB_GREEN, FB_RGB(10, 10, 30));
    bg_draw_string(bg, 42, 210, "Terminal", FB_WHITE, FB_RGB(24, 72, 120));

    /* Icon 2: Files */
    bg_fill_rect(bg, 170, 140, 64, 64, FB_RGB(200, 180, 60));
    bg_fill_rect(bg, 172, 142, 60, 60, FB_RGB(220, 200, 80));
    bg_draw_string(bg, 182, 162, "DIR", FB_RGB(100, 80, 0), FB_RGB(220, 200, 80));
    bg_draw_string(bg, 174, 210, "Files", FB_WHITE, FB_RGB(24, 72, 120));

    /* Icon 3: About */
    bg_fill_rect(bg, 290, 140, 64, 64, FB_RGB(60, 60, 200));
    bg_fill_rect(bg, 292, 142, 60, 60, FB_RGB(80, 80, 220));
    bg_draw_string(bg, 310, 158, "i", FB_WHITE, FB_RGB(80, 80, 220));
    bg_draw_string(bg, 290, 210, "About", FB_WHITE, FB_RGB(26, 78, 130));

    /* Icon 4: System Monitor */
    bg_fill_rect(bg, 410, 140, 64, 64, FB_RGB(40, 140, 180));
    bg_fill_rect(bg, 412, 142, 60, 60, FB_RGB(50, 160, 200));
    bg_draw_string(bg, 418, 154, "SYS", FB_WHITE, FB_RGB(50, 160, 200));
    bg_draw_string(bg, 406, 210, "Monitor", FB_WHITE, FB_RGB(28, 82, 135));

    /* Icon 5: NN Viz */
    bg_fill_rect(bg, 530, 140, 64, 64, FB_RGB(160, 40, 160));
    bg_fill_rect(bg, 532, 142, 60, 60, FB_RGB(180, 60, 180));
    bg_draw_string(bg, 542, 154, "NN", FB_WHITE, FB_RGB(180, 60, 180));
    bg_draw_string(bg, 534, 210, "NeuNet", FB_WHITE, FB_RGB(28, 82, 135));
}

/* Update taskbar in background buffer (clock + process count) */
static void render_taskbar(uint32_t *bg) {
    uint32_t ty = DESKTOP_H - TASKBAR_H;
    uint32_t tb_bg = FB_RGB(40, 40, 60);

    /* Clear taskbar area */
    bg_fill_rect(bg, 0, ty, DESKTOP_W, TASKBAR_H, tb_bg);
    /* Separator line */
    for (uint32_t x = 0; x < DESKTOP_W; x++)
        bg[ty * DESKTOP_W + x] = FB_RGB(80, 80, 120);

    /* PlantOS button */
    bg_fill_rect(bg, 4, ty + 4, 72, 24, FB_RGB(0, 120, 60));
    bg_draw_string(bg, 12, ty + 8, "PlantOS", FB_WHITE, FB_RGB(0, 120, 60));

    /* Process count */
    int nprocs = task_active_count();
    char procbuf[24];
    char nbuf[8];
    utoa(nprocs, nbuf, 10);
    strcpy(procbuf, nbuf);
    strcat(procbuf, " tasks");
    bg_draw_string(bg, 90, ty + 8, procbuf, FB_RGB(160, 160, 180), tb_bg);

    /* Memory usage */
    uint64_t used_kb = pmm_get_used_pages() * 4;
    uint64_t total_kb = pmm_get_total_pages() * 4;
    char membuf[32];
    char u[8], t[8];
    utoa(used_kb / 1024, u, 10);
    utoa(total_kb / 1024, t, 10);
    strcpy(membuf, u);
    strcat(membuf, "/");
    strcat(membuf, t);
    strcat(membuf, "MB");
    bg_draw_string(bg, 200, ty + 8, membuf, FB_RGB(160, 160, 180), tb_bg);

    /* Clock */
    char timebuf[16];
    uint64_t secs = pit_get_ticks() / PIT_FREQ;
    uint64_t mins = secs / 60;
    uint64_t hrs  = mins / 60;
    secs %= 60; mins %= 60;
    timebuf[0] = '0' + (char)(hrs / 10);
    timebuf[1] = '0' + (char)(hrs % 10);
    timebuf[2] = ':';
    timebuf[3] = '0' + (char)(mins / 10);
    timebuf[4] = '0' + (char)(mins % 10);
    timebuf[5] = ':';
    timebuf[6] = '0' + (char)(secs / 10);
    timebuf[7] = '0' + (char)(secs % 10);
    timebuf[8] = '\0';
    bg_draw_string(bg, DESKTOP_W - 80, ty + 8, timebuf,
                   FB_RGB(200, 200, 220), tb_bg);
}

/* Open demo windows */
static void open_about_window(void) {
    int id = wm_create("About PlantOS", 180, 100, 280, 140);
    if (id < 0) return;
    wm_fill_rect(id, 0, 0, 280, 140, FB_RGB(30, 30, 50));
    wm_draw_string(id, 20, 16, "PlantOS v0.15", FB_RGB(0, 255, 100), FB_RGB(30, 30, 50));
    wm_draw_string(id, 20, 40, "A hobby x86_64 OS", FB_WHITE, FB_RGB(30, 30, 50));
    wm_draw_string(id, 20, 60, "Built from scratch", FB_WHITE, FB_RGB(30, 30, 50));
    wm_draw_string(id, 20, 80, "with C and Assembly", FB_WHITE, FB_RGB(30, 30, 50));
    wm_draw_string(id, 20, 108, "Milestone 17: WM", FB_GREY, FB_RGB(30, 30, 50));
}

/* ---- Interactive File Manager ---- */

#define FILEMGR_CW       280
#define FILEMGR_CH       240
#define FILEMGR_MAX_ENT  13
#define FILEMGR_ROW_H    18

static int  filemgr_wm_id = -1;
static char filemgr_path[128] = "/";
static char filemgr_names[FILEMGR_MAX_ENT][64];
static int  filemgr_is_dir[FILEMGR_MAX_ENT];
static int  filemgr_count = 0;

static void filemgr_render(void) {
    int id = filemgr_wm_id;
    if (id < 0) return;

    uint32_t bg = FB_RGB(25, 25, 35);
    wm_fill_rect(id, 0, 0, FILEMGR_CW, FILEMGR_CH, bg);

    /* Path bar */
    wm_fill_rect(id, 0, 0, FILEMGR_CW, 18, FB_RGB(40, 40, 60));
    wm_draw_string(id, 4, 2, filemgr_path, FB_RGB(100, 200, 255), FB_RGB(40, 40, 60));

    /* Read directory */
    filemgr_count = 0;
    int fd = vfs_open(filemgr_path, 0);
    if (fd < 0) {
        wm_draw_string(id, 10, 24, "Cannot open dir", FB_RED, bg);
        return;
    }

    /* Add ".." entry if not at root */
    if (filemgr_path[0] == '/' && filemgr_path[1] != '\0') {
        strcpy(filemgr_names[0], "..");
        filemgr_is_dir[0] = 1;
        filemgr_count = 1;
    }

    struct vfs_dirent de;
    while (vfs_readdir(fd, &de) == 0 && filemgr_count < FILEMGR_MAX_ENT) {
        strcpy(filemgr_names[filemgr_count], de.name);
        /* Build full path for stat */
        char fullpath[128];
        strcpy(fullpath, filemgr_path);
        if (fullpath[strlen(fullpath) - 1] != '/')
            strcat(fullpath, "/");
        strcat(fullpath, de.name);
        struct vfs_stat st;
        filemgr_is_dir[filemgr_count] = 0;
        if (vfs_stat(fullpath, &st) == 0 && st.type == VFS_DIR)
            filemgr_is_dir[filemgr_count] = 1;
        filemgr_count++;
    }
    vfs_close(fd);

    /* Draw entries */
    for (int i = 0; i < filemgr_count; i++) {
        char line[40];
        uint32_t color;
        if (filemgr_is_dir[i]) {
            color = FB_CYAN;
            strcpy(line, filemgr_names[i]);
            strcat(line, "/");
        } else {
            color = FB_WHITE;
            strcpy(line, filemgr_names[i]);
        }
        wm_draw_string(id, 10, 22 + i * FILEMGR_ROW_H, line, color, bg);
    }
}

static void filemgr_open(void) {
    if (filemgr_wm_id >= 0) return; /* already open */
    strcpy(filemgr_path, "/");
    filemgr_wm_id = wm_create("File Manager", 280, 100, FILEMGR_CW, FILEMGR_CH);
    if (filemgr_wm_id < 0) return;
    filemgr_render();
}

static void filemgr_click(int32_t local_y) {
    if (filemgr_wm_id < 0) return;
    if (local_y < 22) return; /* clicked path bar */
    int row = (local_y - 22) / FILEMGR_ROW_H;
    if (row < 0 || row >= filemgr_count) return;

    if (!filemgr_is_dir[row]) return; /* file, not directory */

    if (strcmp(filemgr_names[row], "..") == 0) {
        /* Go up: remove last path component */
        int len = (int)strlen(filemgr_path);
        if (len > 1) {
            /* Remove trailing slash if any */
            if (filemgr_path[len - 1] == '/')
                filemgr_path[--len] = '\0';
            /* Find last slash */
            while (len > 0 && filemgr_path[len - 1] != '/')
                len--;
            if (len == 0) len = 1; /* keep root "/" */
            filemgr_path[len] = '\0';
        }
    } else {
        /* Navigate into subdirectory */
        if (filemgr_path[strlen(filemgr_path) - 1] != '/')
            strcat(filemgr_path, "/");
        strcat(filemgr_path, filemgr_names[row]);
    }

    /* Update title */
    char title[40];
    strcpy(title, "Files: ");
    /* Truncate path for title */
    int plen = (int)strlen(filemgr_path);
    if (plen > 28)
        strcat(title, filemgr_path + plen - 28);
    else
        strcat(title, filemgr_path);
    struct wm_window *w = wm_get(filemgr_wm_id);
    if (w) {
        int i = 0;
        while (title[i] && i < 31) { w->title[i] = title[i]; i++; }
        w->title[i] = '\0';
    }

    filemgr_render();
}

/* ---- System Monitor ---- */

#define SYSMON_CW  300
#define SYSMON_CH  260

static int sysmon_wm_id = -1;

static void sysmon_render(void) {
    int id = sysmon_wm_id;
    if (id < 0) return;

    uint32_t bg = FB_RGB(20, 20, 35);
    wm_fill_rect(id, 0, 0, SYSMON_CW, SYSMON_CH, bg);

    /* Header */
    wm_draw_string(id, 4, 2, "PID  NAME              STATE",
                   FB_RGB(180, 180, 200), bg);
    wm_fill_rect(id, 0, 16, SYSMON_CW, 1, FB_RGB(60, 60, 80));

    /* Task list */
    struct task *head = task_get_list();
    if (!head) return;

    struct task *t = head;
    int row = 0;
    do {
        if (t->state != TASK_UNUSED && row < 10) {
            char line[40];
            char pidbuf[8];
            utoa(t->pid, pidbuf, 10);

            /* Format: "PID  NAME              STATE" */
            int pos = 0;
            /* PID (5 chars) */
            for (int i = 0; pidbuf[i] && pos < 5; i++)
                line[pos++] = pidbuf[i];
            while (pos < 5) line[pos++] = ' ';

            /* Name (18 chars) */
            int nlen = (int)strlen(t->name);
            for (int i = 0; i < nlen && i < 18; i++)
                line[pos++] = t->name[i];
            while (pos < 23) line[pos++] = ' ';

            /* State */
            const char *st;
            uint32_t st_color;
            switch (t->state) {
                case TASK_RUNNING: st = "RUN";  st_color = FB_GREEN; break;
                case TASK_READY:   st = "RDY";  st_color = FB_RGB(100, 200, 100); break;
                case TASK_BLOCKED: st = "BLK";  st_color = FB_RGB(200, 200, 60); break;
                case TASK_STOPPED: st = "STP";  st_color = FB_RGB(200, 100, 60); break;
                case TASK_ZOMBIE:  st = "ZMB";  st_color = FB_RGB(150, 150, 150); break;
                default:           st = "???";  st_color = FB_GREY; break;
            }
            for (int i = 0; st[i]; i++)
                line[pos++] = st[i];
            line[pos] = '\0';

            wm_draw_string(id, 4, 20 + row * 16, line, st_color, bg);
            row++;
        }
        t = t->next;
    } while (t != head);

    /* Separator */
    int info_y = 20 + 10 * 16 + 4;
    wm_fill_rect(id, 0, info_y - 4, SYSMON_CW, 1, FB_RGB(60, 60, 80));

    /* Memory info */
    char membuf[48];
    uint64_t pmm_used = pmm_get_used_pages() * 4;  /* KB */
    uint64_t pmm_total = pmm_get_total_pages() * 4;
    strcpy(membuf, "RAM: ");
    char tmp[12];
    utoa(pmm_used / 1024, tmp, 10);
    strcat(membuf, tmp);
    strcat(membuf, "/");
    utoa(pmm_total / 1024, tmp, 10);
    strcat(membuf, tmp);
    strcat(membuf, " MB");
    wm_draw_string(id, 4, info_y, membuf, FB_RGB(100, 200, 255), bg);

    /* Heap info */
    char heapbuf[48];
    strcpy(heapbuf, "Heap: ");
    utoa(heap_get_used() / 1024, tmp, 10);
    strcat(heapbuf, tmp);
    strcat(heapbuf, "/");
    utoa(heap_get_total() / 1024, tmp, 10);
    strcat(heapbuf, tmp);
    strcat(heapbuf, " KB");
    wm_draw_string(id, 4, info_y + 16, heapbuf, FB_RGB(100, 200, 255), bg);

    /* Uptime */
    char uptbuf[32];
    uint64_t secs = pit_get_ticks() / PIT_FREQ;
    uint64_t mins = secs / 60;
    uint64_t hrs = mins / 60;
    secs %= 60; mins %= 60;
    strcpy(uptbuf, "Up: ");
    utoa(hrs, tmp, 10);
    strcat(uptbuf, tmp);
    strcat(uptbuf, "h ");
    utoa(mins, tmp, 10);
    strcat(uptbuf, tmp);
    strcat(uptbuf, "m ");
    utoa(secs, tmp, 10);
    strcat(uptbuf, tmp);
    strcat(uptbuf, "s");
    wm_draw_string(id, 160, info_y, uptbuf, FB_RGB(180, 180, 200), bg);
}

static void sysmon_open(void) {
    if (sysmon_wm_id >= 0) return;
    sysmon_wm_id = wm_create("System Monitor", 160, 80, SYSMON_CW, SYSMON_CH);
    if (sysmon_wm_id < 0) return;
    sysmon_render();
}

static void open_nn_viz_window(void) {
    /* NN visualization: draw a simple 3-layer feed-forward network diagram */
    uint32_t cw = 340, ch = 260;
    int id = wm_create("Neural Network", 140, 80, cw, ch);
    if (id < 0) return;

    uint32_t bg_c = FB_RGB(15, 15, 30);
    wm_fill_rect(id, 0, 0, cw, ch, bg_c);

    /* Title */
    wm_draw_string(id, 10, 6, "XOR Network (2-4-1)", FB_RGB(0, 220, 120), bg_c);

    /* Layer labels */
    wm_draw_string(id, 20, 240, "Input", FB_RGB(120, 120, 140), bg_c);
    wm_draw_string(id, 130, 240, "Hidden", FB_RGB(120, 120, 140), bg_c);
    wm_draw_string(id, 260, 240, "Output", FB_RGB(120, 120, 140), bg_c);

    /* Network layout:
     * Input layer:  2 neurons at x=40
     * Hidden layer: 4 neurons at x=160
     * Output layer: 1 neuron  at x=280
     */
    int32_t input_x = 40, hidden_x = 160, output_x = 280;
    int32_t input_y[2]  = {90, 170};
    int32_t hidden_y[4] = {50, 110, 170, 230};
    int32_t output_y[1] = {130};

    /* Draw connections: input -> hidden */
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 4; j++) {
            /* Color connections by "weight" (fake gradient for demo) */
            uint32_t c = (j + i) % 2 == 0 ?
                FB_RGB(60, 120, 200) : FB_RGB(200, 80, 60);
            wm_draw_line(id, input_x, input_y[i], hidden_x, hidden_y[j], c);
        }

    /* Draw connections: hidden -> output */
    for (int j = 0; j < 4; j++) {
        uint32_t c = (j % 2 == 0) ?
            FB_RGB(60, 200, 120) : FB_RGB(200, 200, 60);
        wm_draw_line(id, hidden_x, hidden_y[j], output_x, output_y[0], c);
    }

    /* Draw neurons as filled circles */
    /* Input neurons (cyan) */
    for (int i = 0; i < 2; i++) {
        wm_fill_circle(id, input_x, input_y[i], 12, FB_RGB(0, 180, 220));
        wm_draw_circle(id, input_x, input_y[i], 12, FB_RGB(0, 220, 255));
    }
    /* Hidden neurons (purple) */
    for (int j = 0; j < 4; j++) {
        wm_fill_circle(id, hidden_x, hidden_y[j], 12, FB_RGB(140, 60, 200));
        wm_draw_circle(id, hidden_x, hidden_y[j], 12, FB_RGB(180, 100, 255));
    }
    /* Output neuron (green) */
    wm_fill_circle(id, output_x, output_y[0], 12, FB_RGB(40, 200, 80));
    wm_draw_circle(id, output_x, output_y[0], 12, FB_RGB(80, 255, 120));

    /* Neuron labels */
    wm_draw_string(id, input_x - 4, input_y[0] - 6, "x", FB_WHITE, FB_RGB(0, 180, 220));
    wm_draw_string(id, input_x - 4, input_y[1] - 6, "y", FB_WHITE, FB_RGB(0, 180, 220));
    wm_draw_string(id, output_x - 4, output_y[0] - 6, "o", FB_WHITE, FB_RGB(40, 200, 80));

    /* Legend */
    wm_draw_line(id, 10, 30, 30, 30, FB_RGB(60, 120, 200));
    wm_draw_string(id, 34, 22, "+w", FB_RGB(60, 120, 200), bg_c);
    wm_draw_line(id, 70, 30, 90, 30, FB_RGB(200, 80, 60));
    wm_draw_string(id, 94, 22, "-w", FB_RGB(200, 80, 60), bg_c);
}

static void cmd_desktop(int argc, char **argv) {
    (void)argc; (void)argv;
    if (!fb_is_available()) {
        kprintf("No framebuffer available\n");
        return;
    }

    /* Enter graphics mode */
    if (fb_set_mode(DESKTOP_W, DESKTOP_H) < 0) {
        kprintf("Failed to set graphics mode\n");
        return;
    }

    mouse_set_bounds(DESKTOP_W, DESKTOP_H);

    /* Cache LFB pointer for cursor drawing */
    struct fb_info dinfo;
    fb_get_info(&dinfo);
    desktop_lfb = (volatile uint32_t *)dinfo.framebuffer;
    desktop_stride = dinfo.pitch / 4;
    desktop_w = dinfo.width;
    desktop_h = dinfo.height;

    /* Allocate background buffer + compositing backbuffer */
    uint32_t *bg_buf = kmalloc(DESKTOP_W * DESKTOP_H * sizeof(uint32_t));
    uint32_t *backbuf = kmalloc(DESKTOP_W * DESKTOP_H * sizeof(uint32_t));
    if (!bg_buf || !backbuf) {
        fb_text_mode();
        if (bg_buf) kfree(bg_buf);
        if (backbuf) kfree(backbuf);
        kprintf("Out of memory for desktop buffers\n");
        return;
    }

    /* Render background */
    render_background(bg_buf);
    render_background_decorations(bg_buf);
    render_taskbar(bg_buf);

    /* Initialize window manager */
    wm_init(DESKTOP_W, DESKTOP_H);

    /* Drain any stale bytes from 8042 */
    for (int i = 0; i < 32; i++) {
        if (!(inb(0x64) & 0x01)) break;
        inb(0x60);
    }

    /* Re-enable mouse data reporting */
    for (int i = 0; i < 500000; i++) if (!(inb(0x64) & 0x02)) break;
    outb(0x64, 0xD4);
    for (int i = 0; i < 500000; i++) if (!(inb(0x64) & 0x02)) break;
    outb(0x60, 0xF4);
    for (int i = 0; i < 500000; i++) if (inb(0x64) & 0x01) break;
    inb(0x60); /* read ACK */

    /* Initial composite — render to backbuffer, then blit to LFB */
    wm_composite((volatile uint32_t *)backbuf, DESKTOP_W, bg_buf);
    for (uint32_t y = 0; y < DESKTOP_H; y++)
        for (uint32_t x = 0; x < DESKTOP_W; x++)
            desktop_lfb[y * desktop_stride + x] = backbuf[y * DESKTOP_W + x];

    int32_t mx = DESKTOP_W / 2, my = DESKTOP_H / 2;
    int32_t prev_cx = mx, prev_cy = my;
    save_under_cursor(mx, my);
    draw_cursor(mx, my);

    /* Input state */
    bool left_btn = false;
    bool prev_left = false;
    uint8_t mpkt[3];
    int mpkt_idx = 0;
    bool shift_held = false;
    bool kbd_e0 = false;

    /* Drag state */
    bool dragging = false;
    int drag_win = -1;
    int32_t drag_off_x = 0, drag_off_y = 0;

    bool needs_composite = false;
    uint64_t last_clock = 0;
    bool esc_pressed = false;

    while (!esc_pressed) {
        /* Poll 8042 controller directly */
        uint8_t status = inb(0x64);
        if (status & 0x01) {
            uint8_t data = inb(0x60);

            /* Handle keyboard extended prefix regardless of AUX bit */
            if (data == 0xE0 && !kbd_e0) {
                kbd_e0 = true;
            } else if (kbd_e0) {
                kbd_e0 = false;
                if (!(data & 0x80)) {
                    char key = keyboard_extended_to_key(data);
                    if (key) {
                        int term_id = term_get_wm_id();
                        if (term_id >= 0) {
                            term_key_input(key);
                            needs_composite = true;
                        }
                    }
                }
            } else if (status & 0x20) {
                /* Mouse data */
                if (mpkt_idx == 0 && !(data & 0x08)) {
                    /* Not a valid first byte, skip */
                } else {
                    mpkt[mpkt_idx++] = data;
                    if (mpkt_idx >= 3) {
                        mpkt_idx = 0;
                        uint8_t flags = mpkt[0];
                        if (!(flags & 0xC0)) {
                            int32_t dx = (int32_t)mpkt[1];
                            int32_t dy = (int32_t)mpkt[2];
                            if (flags & 0x10) dx |= (int32_t)0xFFFFFF00;
                            if (flags & 0x20) dy |= (int32_t)0xFFFFFF00;
                            dy = -dy;
                            mx += dx;
                            my += dy;
                            if (mx < 0) mx = 0;
                            if (my < 0) my = 0;
                            if (mx >= DESKTOP_W) mx = DESKTOP_W - 1;
                            if (my >= DESKTOP_H) my = DESKTOP_H - 1;
                            left_btn = (flags & 0x01) != 0;
                        }
                    }
                }
            } else {
                /* Keyboard data (0xE0 prefix already handled above) */
                if (data & 0x80) {
                    /* Key release */
                    uint8_t released = data & 0x7F;
                    if (released == 0x2A || released == 0x36)
                        shift_held = false;
                } else {
                    /* Key press */
                    if (data == 0x01) {
                        esc_pressed = true;
                    } else if (data == 0x2A || data == 0x36) {
                        shift_held = true;
                    } else {
                        /* Route to terminal if it's the topmost window */
                        int term_id = term_get_wm_id();
                        if (term_id >= 0) {
                            char c = keyboard_scancode_to_ascii(data, shift_held);
                            if (c) {
                                term_key_input(c);
                                needs_composite = true;
                            }
                        }
                    }
                }
            }
        }

        /* Detect click and release edges */
        bool clicked = (left_btn && !prev_left);
        bool released = (!left_btn && prev_left);
        prev_left = left_btn;

        /* Handle drag */
        if (dragging) {
            if (released) {
                dragging = false;
                drag_win = -1;
            } else {
                /* Move window to follow mouse */
                wm_move(drag_win, mx - drag_off_x, my - drag_off_y);
                needs_composite = true;
            }
        }

        /* Handle click */
        if (clicked && !dragging) {
            int win = wm_window_at(mx, my);
            if (win >= 0) {
                int hit = wm_hit_test(win, mx, my);
                if (hit == WM_HIT_CLOSE) {
                    if (win == term_get_wm_id())
                        term_close();
                    else {
                        if (win == filemgr_wm_id) filemgr_wm_id = -1;
                        if (win == sysmon_wm_id)  sysmon_wm_id = -1;
                        wm_destroy(win);
                    }
                    needs_composite = true;
                } else if (hit == WM_HIT_TITLEBAR) {
                    wm_bring_to_front(win);
                    dragging = true;
                    drag_win = win;
                    struct wm_window *w = wm_get(win);
                    drag_off_x = mx - w->x;
                    drag_off_y = my - w->y;
                    needs_composite = true;
                } else if (hit == WM_HIT_CLIENT) {
                    wm_bring_to_front(win);
                    /* File manager click handling */
                    if (win == filemgr_wm_id) {
                        struct wm_window *w = wm_get(win);
                        if (w) {
                            int32_t local_y = my - w->y - WM_TITLEBAR_H;
                            filemgr_click(local_y);
                        }
                    }
                    needs_composite = true;
                }
            } else {
                /* Clicked on desktop — check icon areas */
                if (mx >= 50 && mx < 114 && my >= 140 && my < 204) {
                    term_open();
                    needs_composite = true;
                } else if (mx >= 170 && mx < 234 && my >= 140 && my < 204) {
                    filemgr_open();
                    needs_composite = true;
                } else if (mx >= 290 && mx < 354 && my >= 140 && my < 204) {
                    open_about_window();
                    needs_composite = true;
                } else if (mx >= 410 && mx < 474 && my >= 140 && my < 204) {
                    sysmon_open();
                    needs_composite = true;
                } else if (mx >= 530 && mx < 594 && my >= 140 && my < 204) {
                    open_nn_viz_window();
                    needs_composite = true;
                }
            }
        }

        /* Update taskbar + system monitor every second */
        uint64_t now = pit_get_ticks() / PIT_FREQ;
        if (now != last_clock) {
            last_clock = now;
            render_taskbar(bg_buf);
            /* Refresh system monitor if open */
            if (sysmon_wm_id >= 0) {
                struct wm_window *sw = wm_get(sysmon_wm_id);
                if (sw && sw->used) {
                    sysmon_render();
                    needs_composite = true;
                } else {
                    sysmon_wm_id = -1;
                }
            }
            if (!needs_composite) {
                /* Direct blit of taskbar region only (not full composite) */
                restore_under_cursor(prev_cx, prev_cy);
                for (uint32_t y = DESKTOP_H - TASKBAR_H; y < DESKTOP_H; y++)
                    for (uint32_t x = 0; x < DESKTOP_W; x++)
                        desktop_lfb[y * desktop_stride + x] = bg_buf[y * DESKTOP_W + x];
                save_under_cursor(prev_cx, prev_cy);
                draw_cursor(prev_cx, prev_cy);
            }
        }

        /* Recomposite if something changed or cursor moved */
        if (needs_composite || mx != prev_cx || my != prev_cy) {
            if (needs_composite) {
                /* Double-buffer: render to backbuf, then blit to LFB */
                wm_composite((volatile uint32_t *)backbuf, DESKTOP_W, bg_buf);
                for (uint32_t y = 0; y < DESKTOP_H; y++)
                    for (uint32_t x = 0; x < DESKTOP_W; x++)
                        desktop_lfb[y * desktop_stride + x] = backbuf[y * DESKTOP_W + x];
                needs_composite = false;
            } else {
                restore_under_cursor(prev_cx, prev_cy);
            }
            save_under_cursor(mx, my);
            draw_cursor(mx, my);
            prev_cx = mx;
            prev_cy = my;
        }
    }

    /* Cleanup */
    term_close();
    filemgr_wm_id = -1;
    sysmon_wm_id = -1;
    wm_destroy_all();
    kfree(bg_buf);
    kfree(backbuf);

    /* Return to text mode */
    fb_text_mode();
    kprintf("Returned from desktop.\n");
}

static void cmd_mousetest(int argc, char **argv) {
    (void)argc; (void)argv;

    /* Show PIC masks */
    uint8_t mask1 = inb(0x21);  /* Master PIC mask */
    uint8_t mask2 = inb(0xA1);  /* Slave PIC mask */
    kprintf("PIC masks: master=0x%02x slave=0x%02x\n", mask1, mask2);
    kprintf("  IRQ2 (cascade): %s\n", (mask1 & 0x04) ? "MASKED" : "unmasked");
    kprintf("  IRQ12 (mouse):  %s\n", (mask2 & 0x10) ? "MASKED" : "unmasked");

    /* Show 8042 controller status */
    uint8_t status = inb(0x64);
    kprintf("8042 status: 0x%02x\n", status);

    /* Re-send enable data reporting */
    kprintf("Re-enabling mouse data reporting...\n");
    /* Wait for input buffer clear */
    for (int i = 0; i < 500000; i++)
        if (!(inb(0x64) & 0x02)) break;
    outb(0x64, 0xD4);
    for (int i = 0; i < 500000; i++)
        if (!(inb(0x64) & 0x02)) break;
    outb(0x60, 0xF4);  /* Enable reporting */
    for (int i = 0; i < 500000; i++)
        if (inb(0x64) & 0x01) break;
    uint8_t ack = inb(0x60);
    kprintf("Mouse enable ACK: 0x%02x\n", ack);

    /* Poll for mouse data for ~5 seconds */
    kprintf("Polling for mouse data (move mouse now, 5 sec)...\n");
    kprintf("Click inside QEMU window first to grab mouse!\n");
    uint64_t start = pit_get_ticks();
    int count = 0;
    while (pit_get_ticks() - start < PIT_FREQ * 5) {
        status = inb(0x64);
        if (status & 0x01) {  /* Any data available */
            uint8_t data = inb(0x60);
            kprintf("  [%s] data=0x%02x\n",
                    (status & 0x20) ? "MOUSE" : "KBD", data);
            count++;
            if (count > 30) {
                kprintf("  ... (truncated)\n");
                break;
            }
        }
        /* Small busy-wait between polls */
        for (volatile int i = 0; i < 10000; i++);
    }
    if (count == 0)
        kprintf("No data received.\n");
    kprintf("Mouse test done.\n");
}

static void cmd_gfxtest(int argc, char **argv) {
    (void)argc; (void)argv;
    if (!fb_is_available()) {
        kprintf("No framebuffer available\n");
        return;
    }
    kprintf("Switching to 640x480 graphics mode...\n");
    if (fb_set_mode(640, 480) < 0) {
        kprintf("Failed to set mode\n");
        return;
    }
    /* Draw color bars */
    uint32_t bar_w = 640 / 8;
    uint32_t colors[] = {
        FB_RED, FB_ORANGE, FB_YELLOW, FB_GREEN,
        FB_CYAN, FB_BLUE, FB_MAGENTA, FB_WHITE
    };
    for (int i = 0; i < 8; i++)
        fb_fill_rect(i * bar_w, 0, bar_w, 480, colors[i]);

    fb_draw_string(16, 16, "PlantOS Framebuffer Test", FB_BLACK, FB_WHITE);
    fb_draw_string(16, 40, "Press any key to return to text mode...", FB_BLACK, FB_YELLOW);

    /* Wait ~3 seconds then return */
    uint64_t start = pit_get_ticks();
    while (pit_get_ticks() - start < PIT_FREQ * 3)
        __asm__ volatile ("hlt");

    fb_text_mode();
    kprintf("Back to text mode.\n");
}

/* ---- kthread: kernel thread + spinlock demo ---- */

static struct {
    spinlock_t   lock;
    volatile int counter;
    volatile int done_count;
    int          iterations;
} kthread_shared;

/* arg is the thread id encoded as a pointer */
static void kthread_worker(void *arg) {
    int id = (int)(uint64_t)arg;

    kprintf("[kthread %d] started\n", id);

    for (int i = 0; i < kthread_shared.iterations; i++) {
        spin_lock(&kthread_shared.lock);
        kthread_shared.counter++;
        spin_unlock(&kthread_shared.lock);
    }

    kprintf("[kthread %d] finished (counter now %d)\n", id, kthread_shared.counter);

    spin_lock(&kthread_shared.lock);
    kthread_shared.done_count++;
    spin_unlock(&kthread_shared.lock);
}

static void cmd_kthread(int argc, char **argv) {
    int num_threads = 4;
    int iterations = 1000;

    if (argc >= 2) {
        num_threads = 0;
        for (int i = 0; argv[1][i]; i++)
            num_threads = num_threads * 10 + (argv[1][i] - '0');
        if (num_threads < 1) num_threads = 1;
        if (num_threads > 16) num_threads = 16;
    }
    if (argc >= 3) {
        iterations = 0;
        for (int i = 0; argv[2][i]; i++)
            iterations = iterations * 10 + (argv[2][i] - '0');
        if (iterations < 1) iterations = 1;
    }

    kprintf("Kernel thread demo: %d threads x %d iterations\n", num_threads, iterations);

    spin_init(&kthread_shared.lock);
    kthread_shared.counter = 0;
    kthread_shared.done_count = 0;
    kthread_shared.iterations = iterations;

    uint64_t start = pit_get_ticks();

    for (int i = 0; i < num_threads; i++) {
        char name[32];
        name[0] = 'k'; name[1] = 't'; name[2] = '-';
        name[3] = '0' + (char)i; name[4] = '\0';
        kthread_create(name, kthread_worker, (void *)(uint64_t)i);
    }

    /* Wait for all threads to complete */
    while (kthread_shared.done_count < num_threads)
        __asm__ volatile ("hlt");

    uint64_t elapsed = pit_get_ticks() - start;

    int expected = num_threads * iterations;
    kprintf("Result: counter = %d (expected %d) — %s\n",
            kthread_shared.counter, expected,
            kthread_shared.counter == expected ? "PASS" : "MISMATCH");
    kprintf("Elapsed: %llu ticks (%llu ms)\n", elapsed, elapsed * 10);
}

/* ---- rm: remove a file ---- */

static void cmd_rm(int argc, char **argv) {
    if (argc < 2) {
        kprintf("Usage: rm <file> [file2 ...]\n");
        return;
    }

    for (int i = 1; i < argc; i++) {
        char resolved[256];
        path_resolve(argv[i], resolved, sizeof(resolved));

        if (vfs_unlink(resolved) < 0) {
            kprintf("rm: cannot remove '%s'\n", argv[i]);
        }
    }
}

/* ---- Security commands ---- */

/* Lookup username from /etc/passwd by uid. Returns pointer to static buf. */
static const char *uid_to_name(uint16_t uid) {
    static char namebuf[32];
    int fd = vfs_open("/etc/passwd", 0);
    if (fd < 0) {
        utoa(uid, namebuf, 10);
        return namebuf;
    }
    char buf[256];
    int n = vfs_read(fd, buf, sizeof(buf) - 1);
    vfs_close(fd);
    if (n <= 0) { utoa(uid, namebuf, 10); return namebuf; }
    buf[n] = '\0';

    /* Parse lines: name:uid:gid:home:shell */
    char *p = buf;
    while (*p) {
        char *line_start = p;
        /* Find end of line */
        while (*p && *p != '\n') p++;
        char saved = *p;
        if (*p) *p++ = '\0';

        /* Parse uid from line */
        char *colon1 = NULL;
        for (char *c = line_start; *c; c++) {
            if (*c == ':') { colon1 = c; break; }
        }
        if (!colon1) continue;
        uint16_t line_uid = 0;
        for (char *c = colon1 + 1; *c && *c != ':'; c++)
            line_uid = line_uid * 10 + (*c - '0');
        if (line_uid == uid) {
            int i = 0;
            while (line_start[i] && line_start[i] != ':' && i < 31)
                { namebuf[i] = line_start[i]; i++; }
            namebuf[i] = '\0';
            return namebuf;
        }
        if (saved == '\0') break;
    }
    utoa(uid, namebuf, 10);
    return namebuf;
}

/* Lookup uid by username in /etc/passwd. Returns uid or -1. */
static int name_to_uid(const char *name, uint16_t *out_uid, uint16_t *out_gid) {
    int fd = vfs_open("/etc/passwd", 0);
    if (fd < 0) return -1;
    char buf[256];
    int n = vfs_read(fd, buf, sizeof(buf) - 1);
    vfs_close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';

    char *p = buf;
    while (*p) {
        char *line_start = p;
        while (*p && *p != '\n') p++;
        if (*p) *p++ = '\0';

        /* Compare name up to first ':' */
        char *colon1 = NULL;
        for (char *c = line_start; *c; c++) {
            if (*c == ':') { colon1 = c; break; }
        }
        if (!colon1) continue;
        *colon1 = '\0';
        if (strcmp(line_start, name) != 0) { *colon1 = ':'; continue; }

        /* Parse uid */
        uint16_t uid = 0;
        char *c = colon1 + 1;
        while (*c && *c != ':') { uid = uid * 10 + (*c - '0'); c++; }
        /* Parse gid */
        uint16_t gid = 0;
        if (*c == ':') {
            c++;
            while (*c && *c != ':') { gid = gid * 10 + (*c - '0'); c++; }
        }
        *out_uid = uid;
        *out_gid = gid;
        return 0;
    }
    return -1;
}

static void cmd_whoami(int argc, char **argv) {
    (void)argc; (void)argv;
    struct task *t = task_current();
    kprintf("%s\n", uid_to_name(t->uid));
}

static void cmd_id(int argc, char **argv) {
    (void)argc; (void)argv;
    struct task *t = task_current();
    kprintf("uid=%d(%s) gid=%d\n", t->uid, uid_to_name(t->uid), t->gid);
}

static void cmd_su(int argc, char **argv) {
    if (argc < 2) {
        kprintf("Usage: su <username>\n");
        return;
    }
    uint16_t new_uid, new_gid;
    if (name_to_uid(argv[1], &new_uid, &new_gid) < 0) {
        kprintf("su: unknown user '%s'\n", argv[1]);
        return;
    }
    struct task *t = task_current();
    if (t->uid != 0 && new_uid != t->uid) {
        kprintf("su: permission denied (must be root)\n");
        return;
    }
    t->uid = new_uid;
    t->gid = new_gid;
    kprintf("Switched to %s (uid=%d, gid=%d)\n", argv[1], new_uid, new_gid);
}

static void cmd_chmod(int argc, char **argv) {
    if (argc < 3) {
        kprintf("Usage: chmod <mode> <file>\n");
        kprintf("  mode: octal (e.g., 755, 644)\n");
        return;
    }

    /* Parse octal mode */
    uint16_t mode = 0;
    for (int i = 0; argv[1][i]; i++) {
        if (argv[1][i] < '0' || argv[1][i] > '7') {
            kprintf("chmod: invalid mode '%s'\n", argv[1]);
            return;
        }
        mode = mode * 8 + (argv[1][i] - '0');
    }

    char resolved[256];
    path_resolve(argv[2], resolved, sizeof(resolved));

    /* Get inode and check ownership */
    struct vfs_stat st;
    if (vfs_stat(resolved, &st) < 0) {
        kprintf("chmod: cannot access '%s'\n", argv[2]);
        return;
    }

    struct task *t = task_current();
    if (t->uid != 0 && t->uid != st.uid) {
        kprintf("chmod: permission denied\n");
        return;
    }

    /* We need to access the inode directly — resolve through ramfs */
    extern struct vfs_inode *ramfs_get_inode(uint32_t idx);
    extern int ramfs_resolve_path(const char *path);
    int ino = ramfs_resolve_path(resolved);
    if (ino < 0) {
        kprintf("chmod: '%s' not in ramfs\n", argv[2]);
        return;
    }
    struct vfs_inode *node = ramfs_get_inode((uint32_t)ino);
    if (!node) {
        kprintf("chmod: cannot find inode\n");
        return;
    }
    node->mode = mode;
    kprintf("'%s' mode set to %o\n", resolved, mode);
}

static void cmd_cpuinfo(int argc, char **argv) {
    (void)argc; (void)argv;
    kprintf("CPU count: %d detected, %d online\n", cpu_count, smp_cpu_count());
    kprintf("BSP APIC ID: %d\n", bsp_apic_id);
    kprintf("\n");
    kprintf("CPU  APIC  STATUS\n");
    kprintf("---  ----  ------\n");
    for (uint32_t i = 0; i < cpu_count; i++) {
        kprintf("%-5d%-6d%s%s\n", i, cpus[i].apic_id,
            cpus[i].online ? "ONLINE" : "OFFLINE",
            cpus[i].apic_id == bsp_apic_id ? " (BSP)" : "");
    }
}

/* ---- true / false builtins ---- */

static void cmd_true(int argc, char **argv) {
    (void)argc; (void)argv;
    extern int last_exit_code;
    last_exit_code = 0;
}

static void cmd_false(int argc, char **argv) {
    (void)argc; (void)argv;
    extern int last_exit_code;
    last_exit_code = 1;
}

/* ---- test / [ builtin ---- */

static int str_to_int(const char *s) {
    int neg = 0, val = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') val = val * 10 + (*s++ - '0');
    return neg ? -val : val;
}

static void cmd_test(int argc, char **argv) {
    extern int last_exit_code;

    /* If invoked as "[", strip trailing "]" */
    int is_bracket = (strcmp(argv[0], "[") == 0);
    if (is_bracket) {
        if (argc < 2 || strcmp(argv[argc - 1], "]") != 0) {
            kprintf("test: missing ']'\n");
            last_exit_code = 2;
            return;
        }
        argc--; /* ignore trailing ] */
    }

    int result = 0; /* 0 = true for shell convention */

    if (argc == 1) {
        /* test with no args -> false */
        result = 1;
    } else if (argc == 2) {
        /* test STRING -- true if non-empty */
        result = (argv[1][0] != '\0') ? 0 : 1;
    } else if (argc == 3) {
        /* Unary operators */
        if (strcmp(argv[1], "-e") == 0) {
            char resolved[256];
            path_resolve(argv[2], resolved, sizeof(resolved));
            struct vfs_stat st;
            result = (vfs_stat(resolved, &st) == 0) ? 0 : 1;
        } else if (strcmp(argv[1], "-f") == 0) {
            char resolved[256];
            path_resolve(argv[2], resolved, sizeof(resolved));
            struct vfs_stat st;
            result = (vfs_stat(resolved, &st) == 0 && st.type == VFS_FILE) ? 0 : 1;
        } else if (strcmp(argv[1], "-d") == 0) {
            char resolved[256];
            path_resolve(argv[2], resolved, sizeof(resolved));
            struct vfs_stat st;
            result = (vfs_stat(resolved, &st) == 0 && st.type == VFS_DIR) ? 0 : 1;
        } else if (strcmp(argv[1], "-n") == 0) {
            result = (argv[2][0] != '\0') ? 0 : 1;
        } else if (strcmp(argv[1], "-z") == 0) {
            result = (argv[2][0] == '\0') ? 0 : 1;
        } else if (strcmp(argv[1], "!") == 0) {
            result = (argv[2][0] != '\0') ? 1 : 0;
        } else {
            kprintf("test: unknown operator '%s'\n", argv[1]);
            result = 2;
        }
    } else if (argc == 4) {
        /* Binary operators: test A OP B */
        const char *a = argv[1];
        const char *op = argv[2];
        const char *b = argv[3];

        if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0) {
            result = (strcmp(a, b) == 0) ? 0 : 1;
        } else if (strcmp(op, "!=") == 0) {
            result = (strcmp(a, b) != 0) ? 0 : 1;
        } else if (strcmp(op, "-eq") == 0) {
            result = (str_to_int(a) == str_to_int(b)) ? 0 : 1;
        } else if (strcmp(op, "-ne") == 0) {
            result = (str_to_int(a) != str_to_int(b)) ? 0 : 1;
        } else if (strcmp(op, "-lt") == 0) {
            result = (str_to_int(a) < str_to_int(b)) ? 0 : 1;
        } else if (strcmp(op, "-gt") == 0) {
            result = (str_to_int(a) > str_to_int(b)) ? 0 : 1;
        } else if (strcmp(op, "-le") == 0) {
            result = (str_to_int(a) <= str_to_int(b)) ? 0 : 1;
        } else if (strcmp(op, "-ge") == 0) {
            result = (str_to_int(a) >= str_to_int(b)) ? 0 : 1;
        } else {
            kprintf("test: unknown operator '%s'\n", op);
            result = 2;
        }
    } else {
        kprintf("test: too many arguments\n");
        result = 2;
    }

    last_exit_code = result;
}

/* ---- export: promote shell var to environment ---- */

static void cmd_export(int argc, char **argv) {
    extern int last_exit_code;
    if (argc < 2) {
        kprintf("Usage: export VAR[=value]\n");
        last_exit_code = 1;
        return;
    }

    for (int a = 1; a < argc; a++) {
        /* Check for VAR=value form */
        char *eq = NULL;
        for (char *p = argv[a]; *p; p++) {
            if (*p == '=') { eq = p; break; }
        }

        if (eq) {
            *eq = '\0';
            const char *name = argv[a];
            const char *val = eq + 1;
            shell_var_set(name, val);
            env_set(name, val);
            *eq = '=';
        } else {
            const char *val = shell_var_get(argv[a]);
            if (val)
                env_set(argv[a], val);
        }
    }
}

/* ---- set: list shell variables ---- */

static void cmd_set(int argc, char **argv) {
    (void)argc; (void)argv;
    extern int last_exit_code;
    const char *name, *value;
    int i = 0;
    kprintf("Shell variables:\n");
    while (shell_var_iter(i, &name, &value) == 0) {
        kprintf("  %s=%s\n", name, value);
        i++;
    }
    if (i == 0) kprintf("  (none)\n");
    kprintf("$? = %d\n", last_exit_code);
}

/* ---- expr: evaluate arithmetic ---- */

static void cmd_expr(int argc, char **argv) {
    extern int last_exit_code;
    if (argc == 2) {
        /* Single number */
        kprintf("%d\n", str_to_int(argv[1]));
        last_exit_code = 0;
        return;
    }
    if (argc != 4) {
        kprintf("Usage: expr NUM OP NUM  (OP: + - * / %%)\n");
        last_exit_code = 2;
        return;
    }

    int a = str_to_int(argv[1]);
    int b = str_to_int(argv[3]);
    const char *op = argv[2];
    int result = 0;

    if (strcmp(op, "+") == 0) result = a + b;
    else if (strcmp(op, "-") == 0) result = a - b;
    else if (strcmp(op, "*") == 0) result = a * b;
    else if (strcmp(op, "/") == 0) {
        if (b == 0) { kprintf("expr: division by zero\n"); last_exit_code = 2; return; }
        result = a / b;
    }
    else if (strcmp(op, "%") == 0) {
        if (b == 0) { kprintf("expr: division by zero\n"); last_exit_code = 2; return; }
        result = a % b;
    }
    else { kprintf("expr: unknown operator '%s'\n", op); last_exit_code = 2; return; }

    kprintf("%d\n", result);
    last_exit_code = (result == 0) ? 1 : 0; /* expr convention: 0 result = exit 1 */
}

/* ---- run: execute a shell script file with control flow ---- */

#define SCRIPT_MAX_LINES 128
#define SCRIPT_LINE_MAX  256

struct script_ctx {
    char lines[SCRIPT_MAX_LINES][SCRIPT_LINE_MAX];
    int  line_count;
    int  pc;
    int  break_flag;
};

static char *script_trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    int len = strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' || s[len-1] == '\r'))
        s[--len] = '\0';
    return s;
}

static int starts_with_kw(const char *line, const char *kw) {
    while (*line == ' ' || *line == '\t') line++;
    int len = strlen(kw);
    if (strncmp(line, kw, len) != 0) return 0;
    char c = line[len];
    return (c == '\0' || c == ' ' || c == '\t' || c == ';');
}

static int find_matching_fi(struct script_ctx *ctx, int start) {
    int depth = 1;
    for (int i = start + 1; i < ctx->line_count; i++) {
        char *l = script_trim(ctx->lines[i]);
        if (starts_with_kw(l, "if")) depth++;
        else if (starts_with_kw(l, "fi")) {
            depth--;
            if (depth == 0) return i;
        }
    }
    return -1;
}

static int find_matching_done(struct script_ctx *ctx, int start) {
    int depth = 1;
    for (int i = start + 1; i < ctx->line_count; i++) {
        char *l = script_trim(ctx->lines[i]);
        if (starts_with_kw(l, "while")) depth++;
        else if (starts_with_kw(l, "done")) {
            depth--;
            if (depth == 0) return i;
        }
    }
    return -1;
}

/* Find next elif/else/fi at same nesting depth */
static int find_next_branch(struct script_ctx *ctx, int start) {
    int depth = 0;
    for (int i = start + 1; i < ctx->line_count; i++) {
        char *l = script_trim(ctx->lines[i]);
        if (starts_with_kw(l, "if")) depth++;
        else if (starts_with_kw(l, "fi")) {
            if (depth == 0) return i;
            depth--;
        }
        else if (depth == 0 && (starts_with_kw(l, "elif") || starts_with_kw(l, "else")))
            return i;
    }
    return -1;
}

/* Extract condition from "if COND; then" or "while COND; do" */
static void extract_condition(const char *line, const char *keyword, char *out, int outsize) {
    const char *cond = line + strlen(keyword);
    while (*cond == ' ' || *cond == '\t') cond++;
    strncpy(out, cond, outsize - 1);
    out[outsize - 1] = '\0';

    /* Strip trailing "; then" or "; do" or "then" or "do" */
    int len = strlen(out);

    /* Try stripping "; then" */
    for (int i = len - 1; i >= 0; i--) {
        if (out[i] != ' ' && out[i] != '\t') {
            if (i >= 3 && strncmp(&out[i-3], "then", 4) == 0) {
                out[i-3] = '\0';
                /* Strip trailing ; and spaces */
                int j = i - 4;
                while (j >= 0 && (out[j] == ' ' || out[j] == ';' || out[j] == '\t'))
                    out[j--] = '\0';
            } else if (i >= 1 && strncmp(&out[i-1], "do", 2) == 0) {
                out[i-1] = '\0';
                int j = i - 2;
                while (j >= 0 && (out[j] == ' ' || out[j] == ';' || out[j] == '\t'))
                    out[j--] = '\0';
            }
            break;
        }
    }
}

static void script_execute(struct script_ctx *ctx);

static void run_body(struct script_ctx *ctx, int from, int to) {
    ctx->pc = from;
    while (ctx->pc < to) {
        char *bl = script_trim(ctx->lines[ctx->pc]);
        if (*bl == '\0' || *bl == '#' || strcmp(bl, "then") == 0 || strcmp(bl, "do") == 0) {
            ctx->pc++;
            continue;
        }
        if (starts_with_kw(bl, "if") || starts_with_kw(bl, "while")) {
            script_execute(ctx);
            if (ctx->break_flag) return;
            continue;
        }
        if (starts_with_kw(bl, "elif") || starts_with_kw(bl, "else") || starts_with_kw(bl, "fi") || starts_with_kw(bl, "done"))
            return;
        if (strcmp(bl, "break") == 0) {
            ctx->break_flag = 1;
            ctx->pc++;
            return;
        }
        shell_execute_line(bl);
        ctx->pc++;
        if (ctx->break_flag) return;
    }
}

static void script_execute(struct script_ctx *ctx) {
    extern int last_exit_code;

    while (ctx->pc < ctx->line_count) {
        char *line = script_trim(ctx->lines[ctx->pc]);

        if (*line == '\0' || *line == '#' || strcmp(line, "then") == 0 ||
            strcmp(line, "do") == 0 || strcmp(line, "fi") == 0 ||
            strcmp(line, "done") == 0) {
            ctx->pc++;
            continue;
        }

        if (strcmp(line, "break") == 0) {
            ctx->break_flag = 1;
            ctx->pc++;
            return;
        }

        /* ---- if/elif/else/fi ---- */
        if (starts_with_kw(line, "if")) {
            int fi_line = find_matching_fi(ctx, ctx->pc);
            if (fi_line < 0) {
                kprintf("script: unmatched 'if' at line %d\n", ctx->pc + 1);
                ctx->pc = ctx->line_count;
                return;
            }

            /* Evaluate if condition */
            char cond[SCRIPT_LINE_MAX];
            extract_condition(line, "if", cond, sizeof(cond));
            shell_execute_line(cond);
            int satisfied = (last_exit_code == 0);

            if (satisfied) {
                /* Find end of if-body (next elif/else/fi at same level) */
                int body_end = find_next_branch(ctx, ctx->pc);
                if (body_end < 0) body_end = fi_line;
                run_body(ctx, ctx->pc + 1, body_end);
                ctx->pc = fi_line + 1;
            } else {
                /* Find elif/else */
                int branch = find_next_branch(ctx, ctx->pc);
                while (branch >= 0 && branch < fi_line) {
                    char *bl = script_trim(ctx->lines[branch]);
                    if (starts_with_kw(bl, "elif")) {
                        char econd[SCRIPT_LINE_MAX];
                        extract_condition(bl, "elif", econd, sizeof(econd));
                        shell_execute_line(econd);
                        if (last_exit_code == 0) {
                            int next = find_next_branch(ctx, branch);
                            if (next < 0) next = fi_line;
                            run_body(ctx, branch + 1, next);
                            ctx->pc = fi_line + 1;
                            goto if_done;
                        }
                        branch = find_next_branch(ctx, branch);
                    } else if (starts_with_kw(bl, "else")) {
                        run_body(ctx, branch + 1, fi_line);
                        ctx->pc = fi_line + 1;
                        goto if_done;
                    } else {
                        break;
                    }
                }
                /* No branch taken */
                ctx->pc = fi_line + 1;
            }
if_done:
            if (ctx->break_flag) return;
            continue;
        }

        /* ---- while/do/done ---- */
        if (starts_with_kw(line, "while")) {
            int done_line = find_matching_done(ctx, ctx->pc);
            if (done_line < 0) {
                kprintf("script: unmatched 'while' at line %d\n", ctx->pc + 1);
                ctx->pc = ctx->line_count;
                return;
            }

            char cond[SCRIPT_LINE_MAX];
            extract_condition(line, "while", cond, sizeof(cond));
            int while_pc = ctx->pc;

            for (;;) {
                shell_execute_line(cond);
                if (last_exit_code != 0) break;

                ctx->break_flag = 0;
                run_body(ctx, while_pc + 1, done_line);
                if (ctx->break_flag) {
                    ctx->break_flag = 0;
                    break;
                }
            }

            ctx->pc = done_line + 1;
            continue;
        }

        /* ---- elif/else/done/fi at top level — skip ---- */
        if (starts_with_kw(line, "elif") || starts_with_kw(line, "else") ||
            starts_with_kw(line, "fi") || starts_with_kw(line, "done")) {
            ctx->pc++;
            return;
        }

        /* ---- Regular command ---- */
        shell_execute_line(line);
        ctx->pc++;
        if (ctx->break_flag) return;
    }
}

static void cmd_run(int argc, char **argv) {
    extern int last_exit_code;
    if (argc < 2) {
        kprintf("Usage: run <script>\n");
        last_exit_code = 1;
        return;
    }

    char resolved[256];
    path_resolve(argv[1], resolved, sizeof(resolved));

    struct vfs_stat st;
    if (vfs_stat(resolved, &st) < 0) {
        kprintf("run: %s: No such file\n", argv[1]);
        last_exit_code = 1;
        return;
    }
    if (st.type != VFS_FILE) {
        kprintf("run: %s: Not a file\n", argv[1]);
        last_exit_code = 1;
        return;
    }

    int fd = vfs_open(resolved, 0);
    if (fd < 0) {
        kprintf("run: %s: Cannot open\n", argv[1]);
        last_exit_code = 1;
        return;
    }

    /* Read entire file (up to 8KB) */
    static char script_buf[8192];
    int total = 0;
    int n;
    while (total < (int)sizeof(script_buf) - 1 &&
           (n = vfs_read(fd, script_buf + total, sizeof(script_buf) - 1 - total)) > 0) {
        total += n;
    }
    script_buf[total] = '\0';
    vfs_close(fd);

    /* Parse lines into script context */
    static struct script_ctx ctx;
    ctx.line_count = 0;
    ctx.pc = 0;
    ctx.break_flag = 0;

    int li = 0;
    for (int i = 0; i <= total; i++) {
        if (script_buf[i] == '\n' || script_buf[i] == '\0') {
            if (ctx.line_count < SCRIPT_MAX_LINES) {
                ctx.lines[ctx.line_count][li] = '\0';
                ctx.line_count++;
            }
            li = 0;
        } else if (li < SCRIPT_LINE_MAX - 1) {
            ctx.lines[ctx.line_count][li++] = script_buf[i];
        }
    }

    script_execute(&ctx);
}

const char *commands_get_name(int index) {
    if (index < 0) return NULL;
    int i = 0;
    while (commands[i].name) {
        if (i == index) return commands[i].name;
        i++;
    }
    return NULL;
}

void commands_init(void) {
    /* Nothing to initialize */
}

void commands_execute(int argc, char **argv) {
    extern int last_exit_code;
    for (int i = 0; commands[i].name; i++) {
        if (strcmp(argv[0], commands[i].name) == 0) {
            last_exit_code = 0;  /* Default success */
            commands[i].func(argc, argv);
            return;
        }
    }
    vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
    kprintf("Unknown command: %s\n", argv[0]);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    kprintf("Type 'help' for available commands.\n");
    last_exit_code = 127;
}
