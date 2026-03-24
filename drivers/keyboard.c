#include "drivers/keyboard.h"
#include "cpu/ports.h"
#include "cpu/idt.h"
#include "lib/printf.h"

static key_handler_t key_callback = NULL;
static bool shift_pressed = false;
static bool ctrl_pressed = false;
static bool caps_lock = false;
static bool e0_prefix = false;

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

char keyboard_extended_to_key(uint8_t scancode) {
    switch (scancode) {
    case 0x48: return KEY_UP;
    case 0x50: return KEY_DOWN;
    case 0x4B: return KEY_LEFT;
    case 0x4D: return KEY_RIGHT;
    case 0x47: return KEY_HOME;
    case 0x4F: return KEY_END;
    case 0x53: return KEY_DELETE;
    case 0x49: return KEY_PGUP;
    case 0x51: return KEY_PGDN;
    default:   return 0;
    }
}

static void keyboard_callback(struct registers *regs) {
    (void)regs;
    uint8_t scancode = inb(0x60);

    /* Handle 0xE0 prefix for extended keys */
    if (scancode == 0xE0) {
        e0_prefix = true;
        return;
    }

    if (e0_prefix) {
        e0_prefix = false;
        /* Ignore extended key releases */
        if (scancode & 0x80) return;
        char key = keyboard_extended_to_key(scancode);
        if (key && key_callback)
            key_callback(key);
        return;
    }

    /* Key release */
    if (scancode & 0x80) {
        uint8_t released = scancode & 0x7F;
        if (released == 0x2A || released == 0x36) /* Left/Right Shift */
            shift_pressed = false;
        if (released == 0x1D) /* Left Ctrl */
            ctrl_pressed = false;
        return;
    }

    /* Key press */
    if (scancode == 0x2A || scancode == 0x36) { /* Shift */
        shift_pressed = true;
        return;
    }
    if (scancode == 0x1D) { /* Left Ctrl */
        ctrl_pressed = true;
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

        /* Ctrl+letter → control code (e.g. Ctrl+S = 0x13, Ctrl+X = 0x18) */
        if (ctrl_pressed && c >= 'a' && c <= 'z')
            c = c - 'a' + 1;

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

key_handler_t keyboard_get_handler(void) {
    return key_callback;
}

char keyboard_scancode_to_ascii(uint8_t scancode, bool shift) {
    if (scancode >= sizeof(scancode_to_ascii))
        return 0;
    if (shift)
        return scancode_to_ascii_shift[scancode];
    return scancode_to_ascii[scancode];
}
