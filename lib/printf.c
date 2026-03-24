#include "lib/printf.h"
#include "lib/string.h"
#include "drivers/vga.h"
#include "drivers/serial.h"

static void (*capture_fn)(char c) = NULL;

static void kput(char c) {
    vga_putchar(c);
    serial_putchar(c);
    if (capture_fn) capture_fn(c);
}

static void kputs(const char *s) {
    while (*s) {
        if (*s == '\n')
            serial_putchar('\r');
        serial_putchar(*s);
        vga_putchar(*s);
        if (capture_fn) capture_fn(*s);
        s++;
    }
}

void kprintf_set_capture(void (*fn)(char c)) {
    capture_fn = fn;
}

void kprintf_clear_capture(void) {
    capture_fn = NULL;
}

static void print_uint(uint64_t val, int base, int width, char pad, int uppercase) {
    char buf[65];
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;

    if (val == 0) {
        buf[i++] = '0';
    } else {
        while (val > 0) {
            buf[i++] = digits[val % base];
            val /= base;
        }
    }
    /* Pad */
    while (i < width)
        buf[i++] = pad;

    /* Print in reverse */
    while (i--)
        kput(buf[i]);
}

static void print_int(int64_t val, int width, char pad) {
    if (val < 0) {
        kput('-');
        print_uint((uint64_t)(-val), 10, width, pad, 0);
    } else {
        print_uint((uint64_t)val, 10, width, pad, 0);
    }
}

void kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            kput(*fmt++);
            continue;
        }
        fmt++; /* skip '%' */

        /* Parse padding */
        char pad = ' ';
        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }

        /* Parse width */
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        /* Parse length modifier */
        int is_long = 0;
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
            if (*fmt == 'l') {
                is_long = 2;
                fmt++;
            }
        }

        switch (*fmt) {
        case 'd':
        case 'i':
            if (is_long >= 2)
                print_int(va_arg(ap, int64_t), width, pad);
            else
                print_int(va_arg(ap, int), width, pad);
            break;
        case 'u':
            if (is_long >= 2)
                print_uint(va_arg(ap, uint64_t), 10, width, pad, 0);
            else
                print_uint(va_arg(ap, unsigned int), 10, width, pad, 0);
            break;
        case 'x':
            if (is_long >= 2)
                print_uint(va_arg(ap, uint64_t), 16, width, pad, 0);
            else
                print_uint(va_arg(ap, unsigned int), 16, width, pad, 0);
            break;
        case 'X':
            if (is_long >= 2)
                print_uint(va_arg(ap, uint64_t), 16, width, pad, 1);
            else
                print_uint(va_arg(ap, unsigned int), 16, width, pad, 1);
            break;
        case 'p':
            kputs("0x");
            print_uint((uint64_t)va_arg(ap, void *), 16, 16, '0', 0);
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            kputs(s);
            break;
        }
        case 'c':
            kput((char)va_arg(ap, int));
            break;
        case '%':
            kput('%');
            break;
        default:
            kput('%');
            kput(*fmt);
            break;
        }
        fmt++;
    }

    va_end(ap);
}
