#include "cpu/fpu.h"
#include "lib/printf.h"
#include "lib/string.h"

void fpu_init(void) {
    /* CR0: set MP (bit 1), clear EM (bit 2), set NE (bit 5) */
    uint64_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= (1UL << 1);    /* MP: monitor coprocessor */
    cr0 &= ~(1UL << 2);   /* EM: clear emulation (allow native FPU) */
    cr0 |= (1UL << 5);    /* NE: native FPU exceptions */
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0));

    /* CR4: set OSFXSR (bit 9) and OSXMMEXCPT (bit 10) */
    uint64_t cr4;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1UL << 9);    /* OSFXSR: enable FXSAVE/FXRSTOR */
    cr4 |= (1UL << 10);   /* OSXMMEXCPT: enable SIMD exceptions */
    __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4));

    /* Initialize the x87 FPU */
    __asm__ volatile ("fninit");

    kprintf("[FPU] FPU/SSE enabled (CR0.MP+NE, CR4.OSFXSR+OSXMMEXCPT)\n");
}

void fpu_save(uint8_t *state) {
    __asm__ volatile ("fxsave (%0)" : : "r"(state) : "memory");
}

void fpu_restore(const uint8_t *state) {
    __asm__ volatile ("fxrstor (%0)" : : "r"(state));
}

void fpu_init_state(uint8_t *state) {
    memset(state, 0, FPU_STATE_SIZE);
    /*
     * FXSAVE format default values:
     *   offset  0: FCW (FPU Control Word) = 0x037F (all exceptions masked)
     *   offset 24: MXCSR (SSE Control/Status) = 0x1F80 (all exceptions masked)
     */
    uint16_t fcw = 0x037F;
    uint32_t mxcsr = 0x1F80;
    memcpy(state + 0, &fcw, 2);
    memcpy(state + 24, &mxcsr, 4);
}
