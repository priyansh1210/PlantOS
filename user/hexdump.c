#include "user/usyscall.h"
#include "user/libc/ulibc.h"

static void print_hex_byte(unsigned char b) {
    const char hex[] = "0123456789abcdef";
    char out[3];
    out[0] = hex[(b >> 4) & 0xF];
    out[1] = hex[b & 0xF];
    out[2] = '\0';
    printf("%s", out);
}

static void print_hex_offset(int off) {
    /* Print 8-digit hex offset */
    for (int i = 28; i >= 0; i -= 4) {
        const char hex[] = "0123456789abcdef";
        printf("%c", hex[(off >> i) & 0xF]);
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: hexdump <file>\n");
        return 1;
    }

    int fd = (int)uopen(argv[1], O_RDONLY);
    if (fd < 0) {
        printf("hexdump: %s: No such file\n", argv[1]);
        return 1;
    }

    unsigned char buf[16];
    int offset = 0;
    int n;

    while ((n = (int)uread(fd, buf, 16)) > 0) {
        print_hex_offset(offset);
        printf("  ");

        /* Hex bytes */
        for (int i = 0; i < 16; i++) {
            if (i == 8) printf(" ");
            if (i < n) {
                print_hex_byte(buf[i]);
                printf(" ");
            } else {
                printf("   ");
            }
        }

        /* ASCII */
        printf(" |");
        for (int i = 0; i < n; i++) {
            char c = buf[i];
            if (c >= 32 && c <= 126)
                printf("%c", c);
            else
                printf(".");
        }
        printf("|\n");

        offset += n;
    }

    /* Final offset line */
    print_hex_offset(offset);
    printf("\n");

    uclose(fd);
    return 0;
}
