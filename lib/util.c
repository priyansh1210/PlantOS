#include "lib/util.h"
#include "lib/string.h"

char *itoa(int64_t value, char *buf, int base) {
    if (base < 2 || base > 16) {
        buf[0] = '\0';
        return buf;
    }

    char *p = buf;
    bool negative = false;

    if (value < 0 && base == 10) {
        negative = true;
        value = -value;
    }

    uint64_t uval = (uint64_t)value;
    char tmp[65];
    int i = 0;
    const char *digits = "0123456789abcdef";

    if (uval == 0) {
        tmp[i++] = '0';
    } else {
        while (uval > 0) {
            tmp[i++] = digits[uval % base];
            uval /= base;
        }
    }

    if (negative)
        *p++ = '-';

    while (i--)
        *p++ = tmp[i];

    *p = '\0';
    return buf;
}

char *utoa(uint64_t value, char *buf, int base) {
    if (base < 2 || base > 16) {
        buf[0] = '\0';
        return buf;
    }

    const char *digits = "0123456789abcdef";
    char tmp[65];
    int i = 0;

    if (value == 0) {
        tmp[i++] = '0';
    } else {
        while (value > 0) {
            tmp[i++] = digits[value % base];
            value /= base;
        }
    }

    char *p = buf;
    while (i--)
        *p++ = tmp[i];
    *p = '\0';
    return buf;
}

int tokenize(char *str, char **tokens, int max_tokens) {
    int count = 0;
    char *p = str;

    while (*p && count < max_tokens) {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '\0')
            break;

        tokens[count++] = p;

        /* Find end of token */
        while (*p && *p != ' ' && *p != '\t')
            p++;
        if (*p) {
            *p = '\0';
            p++;
        }
    }

    return count;
}
