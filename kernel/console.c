#include "kernel/console.h"
#include "drivers/vga.h"
#include "drivers/serial.h"
#include "task/task.h"
#include "task/signal.h"
#include <plantos/signal.h>
#include "lib/string.h"

/* Foreground process PID (0 = no foreground user task) */
static uint64_t fg_pid = 0;

/* Line editing buffer */
#define LINE_MAX 256
static char line_buf[LINE_MAX];
static int  line_pos = 0;

/* Ready buffer — completed lines waiting to be read */
#define READY_BUF_SIZE 1024
static char ready_buf[READY_BUF_SIZE];
static int  ready_read = 0;
static int  ready_write = 0;
static int  ready_count = 0;

/* EOF flag — set by Ctrl+D */
static volatile bool console_eof = false;

/* Block channel for readers */
static int console_channel = 0;

static void ready_push(char c) {
    if (ready_count < READY_BUF_SIZE) {
        ready_buf[ready_write] = c;
        ready_write = (ready_write + 1) % READY_BUF_SIZE;
        ready_count++;
    }
}

void console_init(void) {
    fg_pid = 0;
    line_pos = 0;
    ready_read = 0;
    ready_write = 0;
    ready_count = 0;
    console_eof = false;
}

void console_set_fg(uint64_t pid) {
    fg_pid = pid;
    line_pos = 0;
    ready_count = 0;
    ready_read = 0;
    ready_write = 0;
    console_eof = false;
}

void console_clear_fg(void) {
    fg_pid = 0;
    line_pos = 0;
}

uint64_t console_get_fg(void) {
    return fg_pid;
}

void console_key_handler(char c) {
    /* Ctrl+C — send SIGINT to foreground process */
    if (c == 0x03) {
        if (fg_pid) {
            signal_send(fg_pid, SIGINT);
            /* Also echo ^C and newline */
            vga_putchar('^');
            vga_putchar('C');
            vga_putchar('\n');
            serial_putchar('^');
            serial_putchar('C');
            serial_putchar('\n');
            line_pos = 0;
        }
        return;
    }

    /* Ctrl+Z — send SIGTSTP to foreground process */
    if (c == 0x1A) {
        if (fg_pid) {
            signal_send(fg_pid, SIGTSTP);
            vga_putchar('^');
            vga_putchar('Z');
            vga_putchar('\n');
            serial_putchar('^');
            serial_putchar('Z');
            serial_putchar('\n');
            line_pos = 0;
        }
        return;
    }

    /* Ctrl+D — EOF */
    if (c == 0x04) {
        if (line_pos == 0) {
            /* EOF only when line is empty */
            console_eof = true;
            task_wake_all(&console_channel);
        }
        return;
    }

    /* Backspace */
    if (c == '\b') {
        if (line_pos > 0) {
            line_pos--;
            vga_backspace();
        }
        return;
    }

    /* Enter — submit line */
    if (c == '\n') {
        vga_putchar('\n');
        serial_putchar('\n');
        /* Copy line + newline to ready buffer */
        for (int i = 0; i < line_pos; i++)
            ready_push(line_buf[i]);
        ready_push('\n');
        line_pos = 0;
        /* Wake any blocked reader */
        task_wake_all(&console_channel);
        return;
    }

    /* Ignore special keys (arrow keys etc.) */
    unsigned char uc = (unsigned char)c;
    if (uc >= 0x80) return;

    /* Regular printable character */
    if (c >= 32 && c <= 126 && line_pos < LINE_MAX - 1) {
        line_buf[line_pos++] = c;
        vga_putchar(c);
        serial_putchar(c);
    }
}

int console_read(char *buf, int count) {
    /* Block until data available or EOF */
    while (ready_count == 0 && !console_eof) {
        task_block(&console_channel);
        struct task *cur = task_current();
        if (cur->state == TASK_BLOCKED) {
            __asm__ volatile ("sti");
            while (cur->state == TASK_BLOCKED)
                __asm__ volatile ("hlt");
        }
        /* Check if woken by signal */
        if (cur->pending_signals)
            return -1;
    }

    if (ready_count == 0 && console_eof)
        return 0; /* EOF */

    /* Copy from ready buffer */
    int copied = 0;
    while (copied < count && ready_count > 0) {
        buf[copied++] = ready_buf[ready_read];
        ready_read = (ready_read + 1) % READY_BUF_SIZE;
        ready_count--;
    }
    return copied;
}
