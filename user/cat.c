#include "user/usyscall.h"
#include "user/libc/ulibc.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: cat <file> [file2 ...]\n");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        /* Stat the file first to check it exists */
        struct ustat st;
        if (ufstat(argv[i], &st) < 0) {
            printf("cat: %s: No such file\n", argv[i]);
            continue;
        }
        if (st.type == 2) {
            printf("cat: %s: Is a directory\n", argv[i]);
            continue;
        }

        /* Open and read */
        int fd = (int)uopen(argv[i], O_RDONLY);
        if (fd < 0) {
            printf("cat: %s: Cannot open\n", argv[i]);
            continue;
        }

        char buf[256];
        int n;
        while ((n = (int)uread(fd, buf, sizeof(buf) - 1)) > 0) {
            buf[n] = '\0';
            printf("%s", buf);
        }

        uclose(fd);
    }
    return 0;
}
