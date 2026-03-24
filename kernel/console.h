#ifndef KERNEL_CONSOLE_H
#define KERNEL_CONSOLE_H

#include <plantos/types.h>

/* Initialize the console input subsystem */
void console_init(void);

/* Set/clear the foreground process (receives keyboard input) */
void console_set_fg(uint64_t pid);
void console_clear_fg(void);
uint64_t console_get_fg(void);

/* Keyboard handler for console input mode */
void console_key_handler(char c);

/* Read from console input (blocking). Returns bytes read, 0 on EOF. */
int console_read(char *buf, int count);

#endif
