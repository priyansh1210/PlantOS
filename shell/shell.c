#include "shell/shell.h"
#include "shell/commands.h"
#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "lib/printf.h"
#include "lib/string.h"
#include "lib/util.h"

#define INPUT_MAX 256

static char input_buf[INPUT_MAX];
static int  input_pos = 0;

static void print_prompt(void) {
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    kprintf("plantos");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    kprintf("> ");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

static void process_command(void) {
    input_buf[input_pos] = '\0';

    if (input_pos == 0) {
        print_prompt();
        return;
    }

    char *tokens[16];
    int argc = tokenize(input_buf, tokens, 16);

    if (argc > 0) {
        commands_execute(argc, tokens);
    }

    input_pos = 0;
    print_prompt();
}

static void shell_key_handler(char c) {
    if (c == '\n') {
        kprintf("\n");
        process_command();
    } else if (c == '\b') {
        if (input_pos > 0) {
            input_pos--;
            vga_backspace();
        }
    } else if (input_pos < INPUT_MAX - 1) {
        input_buf[input_pos++] = c;
        kprintf("%c", c);
    }
}

/* Task entry point for the shell */
void shell_task_entry(void) {
    commands_init();
    keyboard_set_handler(shell_key_handler);

    kprintf("\n");
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    kprintf("Type 'help' for available commands.\n\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    print_prompt();

    /* Shell is interrupt-driven — just idle here */
    for (;;)
        __asm__ volatile ("hlt");
}

void shell_init(void) {
    /* Legacy — now use shell_task_entry via task_create */
    shell_task_entry();
}
