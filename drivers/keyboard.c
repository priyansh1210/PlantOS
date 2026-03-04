#include "drivers/keyboard.h"
#include "cpu/ports.h"
#include "cpu/idt.h"
#include "lib/printf.h"

static key_handler_t key_callback = NULL;
static bool shift_pressed = false;
static bool caps_lock = false;

/* US QWERTY scancode to ASCII */
static const char scancode_to_ascii[] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*',  0,   ' '
};

static const char scancode_to_ascii_shift[] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*',  0,   ' '
};

static void keyboard_callback(struct registers *regs) {
    (void)regs;
    uint8_t scancode = inb(0x60);

    /* Key release */
    if (scancode & 0x80) {
        uint8_t released = scancode & 0x7F;
        if (released == 0x2A || released == 0x36) /* Left/Right Shift */
            shift_pressed = false;
        return;
    }

    /* Key press */
    if (scancode == 0x2A || scancode == 0x36) { /* Shift */
        shift_pressed = true;
        return;
    }
    if (scancode == 0x3A) { /* Caps Lock */
        caps_lock = !caps_lock;
        return;
    }

    if (scancode < sizeof(scancode_to_ascii)) {
        char c;
        bool use_shift = shift_pressed;

        /* Caps lock affects letters only */
        if (caps_lock && scancode_to_ascii[scancode] >= 'a' && scancode_to_ascii[scancode] <= 'z')
            use_shift = !use_shift;

        if (use_shift)
            c = scancode_to_ascii_shift[scancode];
        else
            c = scancode_to_ascii[scancode];

        if (c && key_callback)
            key_callback(c);
    }
}

void keyboard_init(void) {
    register_interrupt_handler(33, keyboard_callback); /* IRQ1 = INT 33 */
    kprintf("[KBD] Keyboard driver initialized\n");
}

void keyboard_set_handler(key_handler_t handler) {
    key_callback = handler;
}
