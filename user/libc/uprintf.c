#include "user/libc/ulibc.h"

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)

static int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    size_t pos = 0;

#define PUTC(c) do { if (pos < size - 1) buf[pos] = (c); pos++; } while (0)

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            PUTC(*fmt);
            continue;
        }
        fmt++;

        /* Parse flags */
        int pad_zero = 0;
        int left_align = 0;
        while (*fmt == '0' || *fmt == '-') {
            if (*fmt == '0') pad_zero = 1;
            if (*fmt == '-') left_align = 1;
            fmt++;
        }
        if (left_align) pad_zero = 0;

        /* Parse width */
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        /* Parse length modifier */
        int long_flag = 0;
        if (*fmt == 'l') { long_flag = 1; fmt++; }
        if (*fmt == 'l') { long_flag = 2; fmt++; }

        char numbuf[24];
        const char *str = NULL;
        int slen = 0;

        switch (*fmt) {
        case 'd':
        case 'i': {
            int64_t val;
            if (long_flag >= 1) val = va_arg(ap, int64_t);
            else val = (int64_t)va_arg(ap, int);

            int neg = (val < 0);
            uint64_t uval = neg ? (uint64_t)(-val) : (uint64_t)val;
            int i = 0;
            if (uval == 0) numbuf[i++] = '0';
            else while (uval > 0) { numbuf[i++] = '0' + (uval % 10); uval /= 10; }
            if (neg) numbuf[i++] = '-';
            /* Reverse */
            for (int j = 0; j < i / 2; j++) {
                char t = numbuf[j]; numbuf[j] = numbuf[i-1-j]; numbuf[i-1-j] = t;
            }
            numbuf[i] = '\0';
            str = numbuf; slen = i;
            break;
        }
        case 'u': {
            uint64_t val;
            if (long_flag >= 1) val = va_arg(ap, uint64_t);
            else val = (uint64_t)va_arg(ap, unsigned int);

            int i = 0;
            if (val == 0) numbuf[i++] = '0';
            else while (val > 0) { numbuf[i++] = '0' + (val % 10); val /= 10; }
            for (int j = 0; j < i / 2; j++) {
                char t = numbuf[j]; numbuf[j] = numbuf[i-1-j]; numbuf[i-1-j] = t;
            }
            numbuf[i] = '\0';
            str = numbuf; slen = i;
            break;
        }
        case 'x':
        case 'X': {
            const char *digits = (*fmt == 'x') ? "0123456789abcdef" : "0123456789ABCDEF";
            uint64_t val;
            if (long_flag >= 1) val = va_arg(ap, uint64_t);
            else val = (uint64_t)va_arg(ap, unsigned int);

            int i = 0;
            if (val == 0) numbuf[i++] = '0';
            else while (val > 0) { numbuf[i++] = digits[val & 0xF]; val >>= 4; }
            for (int j = 0; j < i / 2; j++) {
                char t = numbuf[j]; numbuf[j] = numbuf[i-1-j]; numbuf[i-1-j] = t;
            }
            numbuf[i] = '\0';
            str = numbuf; slen = i;
            break;
        }
        case 'p': {
            uint64_t val = (uint64_t)va_arg(ap, void *);
            PUTC('0'); PUTC('x');
            const char *digits = "0123456789abcdef";
            int i = 0;
            if (val == 0) numbuf[i++] = '0';
            else while (val > 0) { numbuf[i++] = digits[val & 0xF]; val >>= 4; }
            for (int j = 0; j < i / 2; j++) {
                char t = numbuf[j]; numbuf[j] = numbuf[i-1-j]; numbuf[i-1-j] = t;
            }
            numbuf[i] = '\0';
            str = numbuf; slen = i;
            width = 0; /* No padding for %p */
            break;
        }
        case 's': {
            str = va_arg(ap, const char *);
            if (!str) str = "(null)";
            slen = 0;
            while (str[slen]) slen++;
            break;
        }
        case 'c': {
            numbuf[0] = (char)va_arg(ap, int);
            numbuf[1] = '\0';
            str = numbuf; slen = 1;
            break;
        }
        case '%':
            PUTC('%');
            continue;
        default:
            PUTC('%');
            PUTC(*fmt);
            continue;
        }

        /* Apply padding */
        int padding = width - slen;
        if (padding < 0) padding = 0;

        if (!left_align) {
            char pad_char = pad_zero ? '0' : ' ';
            for (int i = 0; i < padding; i++) PUTC(pad_char);
        }
        for (int i = 0; i < slen; i++) PUTC(str[i]);
        if (left_align) {
            for (int i = 0; i < padding; i++) PUTC(' ');
        }
    }

    if (pos < size) buf[pos] = '\0';
    else if (size > 0) buf[size - 1] = '\0';

#undef PUTC
    return (int)pos;
}

int printf(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        int len = n;
        if (len > (int)sizeof(buf) - 1) len = (int)sizeof(buf) - 1;
        uwrite(1, buf, (uint64_t)len);
    }
    return n;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}
