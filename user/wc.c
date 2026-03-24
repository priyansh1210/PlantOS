#include "user/usyscall.h"
#include "user/libc/ulibc.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: wc <file> [file2 ...]\n");
        return 1;
    }

    int total_lines = 0, total_words = 0, total_bytes = 0;

    for (int i = 1; i < argc; i++) {
        struct ustat st;
        if (ufstat(argv[i], &st) < 0) {
            printf("wc: %s: No such file\n", argv[i]);
            continue;
        }
        if (st.type == 2) {
            printf("wc: %s: Is a directory\n", argv[i]);
            continue;
        }

        int fd = (int)uopen(argv[i], O_RDONLY);
        if (fd < 0) {
            printf("wc: %s: Cannot open\n", argv[i]);
            continue;
        }

        int lines = 0, words = 0, bytes = 0;
        int in_word = 0;
        char buf[256];
        int n;

        while ((n = (int)uread(fd, buf, sizeof(buf))) > 0) {
            for (int j = 0; j < n; j++) {
                bytes++;
                char c = buf[j];
                if (c == '\n') lines++;
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                    in_word = 0;
                } else {
                    if (!in_word) { words++; in_word = 1; }
                }
            }
        }

        uclose(fd);

        printf("  %d  %d  %d %s\n", lines, words, bytes, argv[i]);
        total_lines += lines;
        total_words += words;
        total_bytes += bytes;
    }

    if (argc > 2) {
        printf("  %d  %d  %d total\n", total_lines, total_words, total_bytes);
    }

    return 0;
}
