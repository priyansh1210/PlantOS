#ifndef USER_LIBC_ULIBC_H
#define USER_LIBC_ULIBC_H

#include <plantos/types.h>
#include "user/usyscall.h"

/* String functions */
size_t  ulib_strlen(const char *s);
int     ulib_strcmp(const char *a, const char *b);
int     ulib_strncmp(const char *a, const char *b, size_t n);
char   *ulib_strcpy(char *dst, const char *src);
char   *ulib_strncpy(char *dst, const char *src, size_t n);
char   *ulib_strcat(char *dst, const char *src);
void   *ulib_memcpy(void *dst, const void *src, size_t n);
void   *ulib_memset(void *s, int c, size_t n);
int     ulib_memcmp(const void *a, const void *b, size_t n);

/* Memory allocation (uses SYS_SBRK) */
void   *malloc(size_t size);
void    free(void *ptr);
void   *realloc(void *ptr, size_t size);

/* Formatted output */
int     printf(const char *fmt, ...);
int     snprintf(char *buf, size_t size, const char *fmt, ...);

/* Convenience macros to use short names */
#define strlen   ulib_strlen
#define strcmp   ulib_strcmp
#define strncmp  ulib_strncmp
#define strcpy   ulib_strcpy
#define strncpy  ulib_strncpy
#define strcat   ulib_strcat
#define memcpy   ulib_memcpy
#define memset   ulib_memset
#define memcmp   ulib_memcmp

/* POSIX-like file I/O wrappers */
static inline int open(const char *path, int flags) {
    int fd = (int)uopen(path, flags);
    if (fd < 0) { errno = ENOENT; return -1; }
    return fd;
}

static inline int close(int fd) {
    return (int)uclose(fd);
}

static inline int read(int fd, void *buf, size_t count) {
    return (int)uread(fd, buf, count);
}

static inline int write(int fd, const void *buf, size_t count) {
    return (int)uwrite(fd, (const char *)buf, count);
}

static inline int lseek(int fd, int offset, int whence) {
    return (int)ulseek(fd, offset, whence);
}

static inline int dup2(int oldfd, int newfd) {
    return (int)udup2(oldfd, newfd);
}

static inline int stat(const char *path, struct ustat *st) {
    return (int)ufstat(path, st);
}

static inline int mkdir(const char *path) {
    return (int)umkdir(path);
}

static inline char *getcwd(char *buf, size_t size) {
    int64_t ret = ugetcwd(buf, size);
    return (ret == -1) ? (char *)0 : buf;
}

static inline int chdir(const char *path) {
    return (int)uchdir(path);
}

static inline const char *getenv(const char *key) {
    return ugetenv(key);
}

static inline int setenv(const char *key, const char *value) {
    return (int)usetenv(key, value);
}

/* Math library */
#include "user/libc/umath.h"

#endif
