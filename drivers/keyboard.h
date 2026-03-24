#ifndef DRIVERS_KEYBOARD_H
#define DRIVERS_KEYBOARD_H

#include <plantos/types.h>

/* Special key codes (above ASCII range) */
#define KEY_UP     0x80
#define KEY_DOWN   0x81
#define KEY_LEFT   0x82
#define KEY_RIGHT  0x83
#define KEY_HOME   0x84
#define KEY_END    0x85
#define KEY_DELETE 0x86
#define KEY_PGUP   0x87
#define KEY_PGDN   0x88
#define KEY_TAB    '\t'

typedef void (*key_handler_t)(char c);

void keyboard_init(void);
void keyboard_set_handler(key_handler_t handler);
key_handler_t keyboard_get_handler(void);

/* Convert a scancode to ASCII (respects shift/caps state) */
char keyboard_scancode_to_ascii(uint8_t scancode, bool shift);

/* Convert extended scancode (after 0xE0 prefix) to special key code */
char keyboard_extended_to_key(uint8_t scancode);

#endif
