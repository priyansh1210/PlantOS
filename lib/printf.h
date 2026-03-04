#ifndef LIB_PRINTF_H
#define LIB_PRINTF_H

#include <plantos/types.h>

/* Variable argument support — compiler builtins */
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_end(ap)         __builtin_va_end(ap)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)

void kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
