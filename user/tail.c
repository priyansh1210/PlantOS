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
    int num_lines = 10;
    int file_arg = 1;

    if (argc >= 3 && strcmp(argv[1], "-n") == 0) {
        num_lines = parse_int(argv[2]);
        if (num_lines < 0) {
            printf("tail: invalid number '%s'\n", argv[2]);
            return 1;
        }
        file_arg = 3;
    }

    if (file_arg >= argc) {
        printf("Usage: tail [-n NUM] <file> [file2 ...]\n");
        return 1;
    }

    for (int i = file_arg; i < argc; i++) {
        if (argc - file_arg > 1)
            printf("==> %s <==\n", argv[i]);

        /* Read entire file into memory */
        struct ustat st;
        if (ufstat(argv[i], &st) < 0) {
            printf("tail: %s: No such file\n", argv[i]);
            continue;
        }
        if (st.size == 0) continue;

        char *data = malloc(st.size + 1);
        if (!data) {
            printf("tail: out of memory\n");
            continue;
        }

        int fd = (int)uopen(argv[i], O_RDONLY);
        if (fd < 0) {
            printf("tail: %s: Cannot open\n", argv[i]);
            free(data);
            continue;
        }

        int total = 0, n;
        while ((n = (int)uread(fd, data + total, st.size - total)) > 0)
            total += n;
        uclose(fd);
        data[total] = '\0';

        /* Count total lines */
        int total_lines = 0;
        for (int j = 0; j < total; j++)
            if (data[j] == '\n') total_lines++;

        /* Skip to the right starting line */
        int skip = total_lines - num_lines;
        if (skip < 0) skip = 0;

        int cur_line = 0;
        for (int j = 0; j < total; j++) {
            if (cur_line >= skip)
                printf("%c", data[j]);
            if (data[j] == '\n')
                cur_line++;
        }

        free(data);
    }
    return 0;
}
