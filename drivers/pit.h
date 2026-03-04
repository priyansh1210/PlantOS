#ifndef DRIVERS_PIT_H
#define DRIVERS_PIT_H

#include <plantos/types.h>

#define PIT_FREQ 100  /* 100 Hz */

void pit_init(void);
uint64_t pit_get_ticks(void);

#endif
