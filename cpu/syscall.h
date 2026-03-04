#ifndef CPU_SYSCALL_H
#define CPU_SYSCALL_H

#include <plantos/types.h>

/* Syscall numbers */
#define SYS_WRITE   0
#define SYS_EXIT    1
#define SYS_YIELD   2
#define SYS_GETPID  3

void syscall_init(void);

/* Assembly stub */
extern void syscall_stub(void);

#endif
