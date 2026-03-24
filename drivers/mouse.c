#include "drivers/mouse.h"
#include "drivers/pic.h"
#include "cpu/ports.h"
#include "cpu/idt.h"
#include "lib/printf.h"

static mouse_handler_t mouse_callback = NULL;
static struct mouse_state state;
static int32_t bound_x = 640;
static int32_t bound_y = 480;

/* PS/2 mouse packet state machine */
static uint8_t packet[3];
static int     packet_idx = 0;

/* Wait for 8042 input buffer to be clear (safe to write) */
static int mouse_wait_write(void) {
    for (int i = 0; i < 500000; i++)
        if (!(inb(0x64) & 0x02)) return 0;
    return -1;
}

/* Wait for 8042 output buffer to have data (safe to read) */
static int mouse_wait_read(void) {
    for (int i = 0; i < 500000; i++)
        if (inb(0x64) & 0x01) return 0;
    return -1;
}

/* Drain any pending bytes from the 8042 controller */
static void mouse_drain(void) {
    for (int i = 0; i < 32; i++) {
        if (!(inb(0x64) & 0x01)) break;
        inb(0x60);
    }
}

/* Send a command to the mouse (via auxiliary port), return ACK byte */
static uint8_t mouse_cmd(uint8_t cmd) {
    mouse_wait_write();
    outb(0x64, 0xD4);   /* Tell controller: next byte goes to mouse */
    mouse_wait_write();
    outb(0x60, cmd);
    if (mouse_wait_read() < 0) return 0;
    return inb(0x60);
}

static void mouse_irq_handler(struct registers *regs) {
    (void)regs;

    uint8_t status = inb(0x64);
    /* Bit 0 = output buffer full, Bit 5 = from auxiliary (mouse) */
    if (!(status & 0x21)) return;
    if (!(status & 0x20)) {
        /* Keyboard data, not mouse — read and discard to clear buffer */
        inb(0x60);
        return;
    }

    uint8_t data = inb(0x60);

    /* First byte must have bit 3 set (always-1 bit in PS/2 protocol) */
    if (packet_idx == 0 && !(data & 0x08)) return;

    packet[packet_idx++] = data;
    if (packet_idx < 3) return;

    /* Full 3-byte packet received */
    packet_idx = 0;

    uint8_t flags = packet[0];

    /* Discard overflow packets */
    if (flags & 0xC0) return;

    /* Decode delta with sign extension */
    int32_t dx = (int32_t)packet[1];
    int32_t dy = (int32_t)packet[2];
    if (flags & 0x10) dx |= (int32_t)0xFFFFFF00;  /* sign extend X */
    if (flags & 0x20) dy |= (int32_t)0xFFFFFF00;  /* sign extend Y */

    /* PS/2 Y axis is inverted (positive = up) */
    dy = -dy;

    state.x += dx;
    state.y += dy;

    /* Clamp to bounds */
    if (state.x < 0) state.x = 0;
    if (state.y < 0) state.y = 0;
    if (state.x >= bound_x) state.x = bound_x - 1;
    if (state.y >= bound_y) state.y = bound_y - 1;

    state.left   = (flags & 0x01) != 0;
    state.right  = (flags & 0x02) != 0;
    state.middle = (flags & 0x04) != 0;

    if (mouse_callback)
        mouse_callback(&state);
}

void mouse_init(void) {
    /* Drain any stale bytes */
    mouse_drain();

    /* Disable both PS/2 ports during configuration */
    mouse_wait_write();
    outb(0x64, 0xAD);  /* Disable keyboard */
    mouse_wait_write();
    outb(0x64, 0xA7);  /* Disable mouse */

    /* Drain again after disable */
    mouse_drain();

    /* Read controller config byte */
    mouse_wait_write();
    outb(0x64, 0x20);
    mouse_wait_read();
    uint8_t config = inb(0x60);

    /* Enable IRQ1 (keyboard) and IRQ12 (mouse), disable translation */
    config |= 0x03;    /* Bits 0,1: enable IRQ1 and IRQ12 */
    config &= ~0x20;   /* Bit 5: enable auxiliary clock (clear = enabled) */
    mouse_wait_write();
    outb(0x64, 0x60);  /* Write config byte */
    mouse_wait_write();
    outb(0x60, config);

    /* Re-enable both ports */
    mouse_wait_write();
    outb(0x64, 0xAE);  /* Enable keyboard */
    mouse_wait_write();
    outb(0x64, 0xA8);  /* Enable auxiliary (mouse) port */

    /* Reset mouse — send 0xFF, expect ACK(0xFA), self-test(0xAA), ID(0x00) */
    mouse_cmd(0xFF);
    /* Wait longer for self-test (can take several hundred ms) */
    for (int i = 0; i < 2; i++) {
        if (mouse_wait_read() == 0)
            inb(0x60);  /* Read self-test result / mouse ID */
    }

    /* Drain any extra bytes from reset */
    mouse_drain();

    /* Set defaults */
    mouse_cmd(0xF6);

    /* Enable data reporting */
    uint8_t ack = mouse_cmd(0xF4);

    /* Unmask IRQ12 on PIC (and IRQ2 for cascade) */
    pic_clear_mask(2);
    pic_clear_mask(12);

    /* Register handler BEFORE enabling interrupts for this IRQ */
    register_interrupt_handler(44, mouse_irq_handler); /* IRQ12 = INT 44 */

    /* Start cursor in center */
    state.x = bound_x / 2;
    state.y = bound_y / 2;
    state.left = state.right = state.middle = false;

    kprintf("[MOUSE] PS/2 mouse initialized (ack=0x%02x)\n", ack);
}

void mouse_set_handler(mouse_handler_t handler) {
    mouse_callback = handler;
}

void mouse_get_state(struct mouse_state *out) {
    *out = state;
}

void mouse_poll(void) {
    /* Check if mouse data is available (status bit 0 + bit 5) */
    uint8_t status = inb(0x64);
    if ((status & 0x21) == 0x21) {
        /* Mouse data available — process it like the IRQ handler */
        uint8_t data = inb(0x60);

        if (packet_idx == 0 && !(data & 0x08)) return;

        packet[packet_idx++] = data;
        if (packet_idx < 3) return;

        packet_idx = 0;
        uint8_t flags = packet[0];
        if (flags & 0xC0) return;

        int32_t dx = (int32_t)packet[1];
        int32_t dy = (int32_t)packet[2];
        if (flags & 0x10) dx |= (int32_t)0xFFFFFF00;
        if (flags & 0x20) dy |= (int32_t)0xFFFFFF00;
        dy = -dy;

        state.x += dx;
        state.y += dy;
        if (state.x < 0) state.x = 0;
        if (state.y < 0) state.y = 0;
        if (state.x >= bound_x) state.x = bound_x - 1;
        if (state.y >= bound_y) state.y = bound_y - 1;

        state.left   = (flags & 0x01) != 0;
        state.right  = (flags & 0x02) != 0;
        state.middle = (flags & 0x04) != 0;

        if (mouse_callback)
            mouse_callback(&state);
    }
}

void mouse_set_bounds(int32_t max_x, int32_t max_y) {
    bound_x = max_x;
    bound_y = max_y;
    state.x = max_x / 2;
    state.y = max_y / 2;
}
