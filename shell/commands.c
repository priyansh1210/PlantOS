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
#include "fs/elf_loader.h"

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
    kprintf("PlantOS v0.5\n");
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
    kprintf("PID  NAME                 STATE    TICKS\n");
    kprintf("---  ----                 -----    -----\n");

    struct task *head = task_get_list();
    if (!head) return;

    struct task *t = head;
    do {
        if (t->state != TASK_UNUSED) {
            char pidbuf[12];
            utoa(t->pid, pidbuf, 10);
            pad_right(pidbuf, 5);
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

static void cmd_ls(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "/";
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
            if (st.type == VFS_DIR) {
                vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
                kprintf("  %s/\n", de.name);
                vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            } else {
                kprintf("  %s  (%llu bytes)\n", de.name, (uint64_t)st.size);
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
    int fd = vfs_open(argv[1], 0);
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
    int fd = vfs_open(argv[1], VFS_O_CREATE);
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
    if (vfs_mkdir(argv[1]) < 0) {
        kprintf("mkdir: cannot create '%s'\n", argv[1]);
    }
}

static void cmd_write(int argc, char **argv) {
    if (argc < 3) {
        kprintf("Usage: write <file> <text...>\n");
        return;
    }
    int fd = vfs_open(argv[1], VFS_O_CREATE);
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
        kprintf("Usage: exec <path>\n");
        return;
    }
    struct elf_info info;
    if (elf_load(argv[1], &info) < 0) {
        kprintf("exec: failed to load '%s'\n", argv[1]);
        return;
    }
    struct task *t = task_create_user_elf(argv[1], info.entry,
                                          info.load_base, info.num_pages);
    if (t) {
        kprintf("Launched ELF task '%s' (pid %llu)\n", argv[1], t->pid);
    } else {
        kprintf("exec: failed to create task\n");
        elf_unload(info.load_base, info.num_pages);
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

void commands_init(void) {
    /* Nothing to initialize */
}

void commands_execute(int argc, char **argv) {
    for (int i = 0; commands[i].name; i++) {
        if (strcmp(argv[0], commands[i].name) == 0) {
            commands[i].func(argc, argv);
            return;
        }
    }
    vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
    kprintf("Unknown command: %s\n", argv[0]);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    kprintf("Type 'help' for available commands.\n");
}
