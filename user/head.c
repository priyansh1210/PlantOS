#include "user/usyscall.h"
#include "user/libc/ulibc.h"

static int parse_int(const char *s) {
    int n = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        n = n * 10 + (s[i] - '0');
    }
    return n;
}

int main(int argc, char **argv) {
    int num_lines = 10; /* default */
    int file_arg = 1;

    /* Parse -n NUM */
    if (argc >= 3 && strcmp(argv[1], "-n") == 0) {
        num_lines = parse_int(argv[2]);
        if (num_lines < 0) {
            printf("head: invalid number '%s'\n", argv[2]);
            return 1;
        }
        file_arg = 3;
    }

    if (file_arg >= argc) {
        printf("Usage: head [-n NUM] <file> [file2 ...]\n");
        return 1;
    }

    for (int i = file_arg; i < argc; i++) {
        if (argc - file_arg > 1)
            printf("==> %s <==\n", argv[i]);

        int fd = (int)uopen(argv[i], O_RDONLY);
        if (fd < 0) {
            printf("head: %s: No such file\n", argv[i]);
            continue;
        }

        int lines = 0;
        char buf[256];
        int n;
        int done = 0;
        while (!done && (n = (int)uread(fd, buf, sizeof(buf) - 1)) > 0) {
            for (int j = 0; j < n && !done; j++) {
                char ch = buf[j];
                printf("%c", ch);
                if (ch == '\n') {
                    lines++;
                    if (lines >= num_lines)
                        done = 1;
                }
            }
        }

        uclose(fd);
    }
    return 0;
}
