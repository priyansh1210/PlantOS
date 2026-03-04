#ifndef DRIVERS_SERIAL_H
#define DRIVERS_SERIAL_H

#include <plantos/types.h>

#define COM1 0x3F8

void serial_init(void);
void serial_putchar(char c);
void serial_puts(const char *str);

#endif
