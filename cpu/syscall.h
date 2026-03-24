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
#define SYS_FORK      11
#define SYS_WAITPID   12
#define SYS_SBRK      13
#define SYS_MMAP      14
#define SYS_MUNMAP    15
#define SYS_OPEN      16
#define SYS_FSTAT     17
#define SYS_MKDIR     18
#define SYS_FBCTL     19  /* Framebuffer control: arg0=subcmd, arg1/arg2=params */
#define SYS_LSEEK     20
#define SYS_DUP2      21
#define SYS_GETCWD    22
#define SYS_CHDIR     23
#define SYS_GETENV    24
#define SYS_SETENV    25
#define SYS_READDIR   26
#define SYS_SOCKET    27  /* Socket operations: arg0=subcmd, arg1/arg2=params */
#define SYS_UNLINK    28
#define SYS_GETUID    29  /* Returns (uid << 16) | gid */
#define SYS_SETUID    30  /* arg0=uid, arg1=gid (root only, or own uid) */

void syscall_init(void);

/* Assembly stub */
extern void syscall_stub(void);

#endif
