#ifndef USER_USYSCALL_H
#define USER_USYSCALL_H

#include <plantos/types.h>

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
#define SYS_FBCTL     19
#define SYS_LSEEK     20
#define SYS_DUP2      21
#define SYS_GETCWD    22
#define SYS_CHDIR     23
#define SYS_GETENV    24
#define SYS_SETENV    25
#define SYS_READDIR   26
#define SYS_SOCKET    27
#define SYS_UNLINK    28
#define SYS_GETUID    29
#define SYS_SETUID    30

/* Socket subcmd constants */
#define SOCK_SUBCMD_SOCKET   0
#define SOCK_SUBCMD_CONNECT  1
#define SOCK_SUBCMD_SEND     2
#define SOCK_SUBCMD_RECV     3
#define SOCK_SUBCMD_CLOSE    4
#define SOCK_SUBCMD_LISTEN   5
#define SOCK_SUBCMD_ACCEPT   6
#define SOCK_SUBCMD_RESOLVE    7
#define SOCK_SUBCMD_UDP_BIND   8
#define SOCK_SUBCMD_UDP_SENDTO 9
#define SOCK_SUBCMD_UDP_RECVFROM 10
#define SOCK_SUBCMD_UDP_CLOSE  11

#define AF_INET       2
#define SOCK_STREAM   1
#define SOCK_DGRAM    2

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

static inline int64_t syscall2(uint64_t num, uint64_t a0, uint64_t a1) {
    int64_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(a0), "S"(a1)
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
    for (;;);
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

static inline int64_t ukill(uint64_t pid, int signum) {
    return syscall2(SYS_KILL, pid, (uint64_t)signum);
}

static inline int64_t usignal(int signum, void (*handler)(int)) {
    return syscall2(SYS_SIGNAL, (uint64_t)signum, (uint64_t)handler);
}

static inline void usigreturn(void) {
    syscall0(SYS_SIGRETURN);
}

static inline int64_t upipe(int *fds) {
    return syscall1(SYS_PIPE, (uint64_t)fds);
}

static inline int64_t uread(int fd, void *buf, uint64_t count) {
    return syscall3(SYS_READ, (uint64_t)fd, (uint64_t)buf, count);
}

static inline int64_t uclose(int fd) {
    return syscall1(SYS_CLOSE, (uint64_t)fd);
}

static inline int64_t ufork(void) {
    return syscall0(SYS_FORK);
}

static inline int64_t uwaitpid(uint64_t pid) {
    return syscall1(SYS_WAITPID, pid);
}

static inline int64_t usbrk(int64_t increment) {
    return syscall1(SYS_SBRK, (uint64_t)increment);
}

/* mmap: allocate virtual memory. Returns mapped address or -1 on failure.
 *   addr = hint (0 = let kernel choose)
 *   length = size in bytes
 *   prot = VMA_READ | VMA_WRITE | VMA_EXEC */
static inline int64_t ummap(uint64_t addr, uint64_t length, uint64_t prot) {
    return syscall3(SYS_MMAP, addr, length, prot);
}

/* munmap: free virtual memory */
static inline int64_t umunmap(uint64_t addr, uint64_t length) {
    return syscall2(SYS_MUNMAP, addr, length);
}

/* Standard file descriptors */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* File I/O */
#define O_RDONLY  0x00
#define O_WRONLY  0x01
#define O_RDWR    0x02
#define O_CREATE  0x40
#define O_TRUNC   0x200

static inline int64_t uopen(const char *path, int flags) {
    return syscall2(SYS_OPEN, (uint64_t)path, (uint64_t)flags);
}

struct ustat {
    uint32_t type;   /* 1=file, 2=dir */
    uint32_t size;
};

static inline int64_t ufstat(const char *path, struct ustat *st) {
    return syscall2(SYS_FSTAT, (uint64_t)path, (uint64_t)st);
}

static inline int64_t umkdir(const char *path) {
    return syscall1(SYS_MKDIR, (uint64_t)path);
}

/* lseek */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

static inline int64_t ulseek(int fd, int offset, int whence) {
    return syscall3(SYS_LSEEK, (uint64_t)fd, (uint64_t)offset, (uint64_t)whence);
}

/* dup2 */
static inline int64_t udup2(int oldfd, int newfd) {
    return syscall2(SYS_DUP2, (uint64_t)oldfd, (uint64_t)newfd);
}

/* getcwd / chdir */
static inline int64_t ugetcwd(char *buf, size_t size) {
    return syscall2(SYS_GETCWD, (uint64_t)buf, (uint64_t)size);
}

static inline int64_t uchdir(const char *path) {
    return syscall1(SYS_CHDIR, (uint64_t)path);
}

/* environment variables */
static inline const char *ugetenv(const char *key) {
    return (const char *)syscall1(SYS_GETENV, (uint64_t)key);
}

static inline int64_t usetenv(const char *key, const char *value) {
    return syscall2(SYS_SETENV, (uint64_t)key, (uint64_t)value);
}

/* readdir */
struct udirent {
    char     name[60];
    uint32_t inode;
};

static inline int64_t ureaddir(int fd, struct udirent *de) {
    return syscall2(SYS_READDIR, (uint64_t)fd, (uint64_t)de);
}

/* errno support */
static int __attribute__((unused)) __errno_val = 0;
#define errno __errno_val

#define ENOENT  2
#define EBADF   9
#define ENOMEM 12
#define EEXIST 17
#define ENOTDIR 20
#define EINVAL 22
#define ENOSYS 38

/* Framebuffer control */
struct ufb_info {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint64_t framebuffer;
};

/* ---- Socket API ---- */

static inline int64_t usocket(int domain, int type) {
    (void)domain; (void)type;
    return syscall3(SYS_SOCKET, SOCK_SUBCMD_SOCKET, (uint64_t)domain, (uint64_t)type);
}

static inline int64_t uconnect(uint32_t ip, uint16_t port) {
    return syscall3(SYS_SOCKET, SOCK_SUBCMD_CONNECT, (uint64_t)ip, (uint64_t)port);
}

static inline int64_t usend(int conn, const void *buf, uint16_t len) {
    uint64_t packed = ((uint64_t)len << 32) | (uint32_t)conn;
    return syscall3(SYS_SOCKET, SOCK_SUBCMD_SEND, packed, (uint64_t)buf);
}

static inline int64_t urecv(int conn, void *buf, uint16_t len) {
    uint64_t packed = ((uint64_t)len << 32) | (uint32_t)conn;
    return syscall3(SYS_SOCKET, SOCK_SUBCMD_RECV, packed, (uint64_t)buf);
}

static inline int64_t usockclose(int conn) {
    return syscall2(SYS_SOCKET, SOCK_SUBCMD_CLOSE, (uint64_t)conn);
}

static inline int64_t ulisten(uint16_t port) {
    return syscall2(SYS_SOCKET, SOCK_SUBCMD_LISTEN, (uint64_t)port);
}

static inline int64_t uaccept(int listen_conn) {
    return syscall2(SYS_SOCKET, SOCK_SUBCMD_ACCEPT, (uint64_t)listen_conn);
}

static inline int64_t uresolve(const char *hostname, uint32_t *ip_out) {
    return syscall3(SYS_SOCKET, SOCK_SUBCMD_RESOLVE, (uint64_t)hostname, (uint64_t)ip_out);
}

/* UDP socket wrappers */
static inline int64_t uudp_bind(uint16_t port) {
    return syscall2(SYS_SOCKET, SOCK_SUBCMD_UDP_BIND, (uint64_t)port);
}

static inline int64_t uudp_sendto(int sock, uint32_t dst_ip, uint16_t dst_port,
                                   const void *buf, uint16_t len) {
    uint64_t params[5];
    params[0] = (uint64_t)sock;
    params[1] = (uint64_t)dst_ip;
    params[2] = (uint64_t)dst_port;
    params[3] = (uint64_t)buf;
    params[4] = (uint64_t)len;
    return syscall2(SYS_SOCKET, SOCK_SUBCMD_UDP_SENDTO, (uint64_t)params);
}

static inline int64_t uudp_recvfrom(int sock, void *buf, uint16_t len,
                                     uint32_t *src_ip, uint16_t *src_port) {
    uint64_t params[5];
    params[0] = (uint64_t)sock;
    params[1] = (uint64_t)buf;
    params[2] = (uint64_t)len;
    params[3] = (uint64_t)src_ip;
    params[4] = (uint64_t)src_port;
    return syscall2(SYS_SOCKET, SOCK_SUBCMD_UDP_RECVFROM, (uint64_t)params);
}

static inline int64_t uudp_close(int sock) {
    return syscall2(SYS_SOCKET, SOCK_SUBCMD_UDP_CLOSE, (uint64_t)sock);
}

/* unlink */
static inline int64_t uunlink(const char *path) {
    return syscall1(SYS_UNLINK, (uint64_t)path);
}

/* uid/gid */
static inline uint16_t ugetuid(void) {
    return (uint16_t)(syscall0(SYS_GETUID) >> 16);
}

static inline uint16_t ugetgid(void) {
    return (uint16_t)(syscall0(SYS_GETUID) & 0xFFFF);
}

static inline int64_t usetuid(uint16_t uid, uint16_t gid) {
    return syscall2(SYS_SETUID, (uint64_t)uid, (uint64_t)gid);
}

/* FBCTL subcmds: 0=getinfo, 1=setmode, 2=textmode */
static inline int64_t ufb_getinfo(struct ufb_info *info) {
    return syscall2(SYS_FBCTL, 0, (uint64_t)info);
}

static inline int64_t ufb_setmode(uint32_t width, uint32_t height) {
    return syscall3(SYS_FBCTL, 1, (uint64_t)width, (uint64_t)height);
}

static inline int64_t ufb_textmode(void) {
    return syscall1(SYS_FBCTL, 2);
}

#endif
