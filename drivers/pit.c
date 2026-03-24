#include "drivers/pit.h"
#include "cpu/ports.h"
#include "cpu/idt.h"
#include "lib/printf.h"
#include "net/tcp.h"

static volatile uint64_t pit_ticks = 0;

static void pit_callback(struct registers *regs) {
    (void)regs;
    pit_ticks++;

    /* TCP retransmission timer — every 10 ticks (~100ms) */
    if ((pit_ticks % 10) == 0)
        tcp_timer();
}

void pit_init(void) {
    /* Register timer handler on IRQ0 (INT 32) */
    register_interrupt_handler(32, pit_callback);

    /* Set PIT frequency */
    uint32_t divisor = 1193180 / PIT_FREQ;
    outb(0x43, 0x36);                       /* Channel 0, lo/hi, rate generator */
    outb(0x40, (uint8_t)(divisor & 0xFF));  /* Low byte */
    outb(0x40, (uint8_t)(divisor >> 8));    /* High byte */

    kprintf("[PIT] Timer initialized at %d Hz\n", PIT_FREQ);
}

uint64_t pit_get_ticks(void) {
    return pit_ticks;
}
