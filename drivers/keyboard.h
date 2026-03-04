#ifndef DRIVERS_KEYBOARD_H
#define DRIVERS_KEYBOARD_H

#include <plantos/types.h>

typedef void (*key_handler_t)(char c);

void keyboard_init(void);
void keyboard_set_handler(key_handler_t handler);

#endif
