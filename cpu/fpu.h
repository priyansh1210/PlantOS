#ifndef CPU_FPU_H
#define CPU_FPU_H

#include <plantos/types.h>

#define FPU_STATE_SIZE 512

/* Initialize FPU/SSE hardware (CR0/CR4 setup) */
void fpu_init(void);

/* Save current FPU/SSE state via FXSAVE */
void fpu_save(uint8_t *state);

/* Restore FPU/SSE state via FXRSTOR */
void fpu_restore(const uint8_t *state);

/* Fill buffer with default FPU/SSE state for a new task */
void fpu_init_state(uint8_t *state);

#endif
