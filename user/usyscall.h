#ifndef USER_USYSCALL_H
#define USER_USYSCALL_H

#include <plantos/types.h>

#define SYS_WRITE   0
#define SYS_EXIT    1
#define SYS_YIELD   2
#define SYS_GETPID  3
#define SYS_EXEC    4

static inline int64_t syscall0(uint64_t num) {
    int64_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num)
        : "memory"
    );
    return ret;
}

static inline int64_t syscall1(uint64_t num, uint64_t a0) {
    int64_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(a0)
        : "memory"
    );
    return ret;
}

static inline int64_t syscall3(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2) {
    int64_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(a0), "S"(a1), "d"(a2)
        : "memory"
    );
    return ret;
}

static inline int64_t uwrite(int fd, const char *buf, uint64_t len) {
    return syscall3(SYS_WRITE, (uint64_t)fd, (uint64_t)buf, len);
}

static inline void uexit(int code) {
    syscall1(SYS_EXIT, (uint64_t)code);
    for (;;); /* noreturn */
}

static inline void uyield(void) {
    syscall0(SYS_YIELD);
}

static inline uint64_t ugetpid(void) {
    return (uint64_t)syscall0(SYS_GETPID);
}

static inline int64_t uexec(const char *path) {
    return syscall1(SYS_EXEC, (uint64_t)path);
}

#endif
