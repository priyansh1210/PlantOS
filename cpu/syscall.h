#ifndef CPU_SYSCALL_H
#define CPU_SYSCALL_H

#include <plantos/types.h>

/* Syscall numbers */
#define SYS_WRITE     0
#define SYS_EXIT      1
#define SYS_YIELD     2
#define SYS_GETPID    3
#define SYS_EXEC      4
#define SYS_KILL      5
#define SYS_SIGNAL    6
#define SYS_SIGRETURN 7
#define SYS_PIPE      8
#define SYS_READ      9
#define SYS_CLOSE     10

void syscall_init(void);

/* Assembly stub */
extern void syscall_stub(void);

#endif
