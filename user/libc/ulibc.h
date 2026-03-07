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

#endif
