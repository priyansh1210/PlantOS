#include "user/usyscall.h"
#include "user/libc/ulibc.h"

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: cp <source> <dest>\n");
        return 1;
    }

    struct ustat st;
    if (ufstat(argv[1], &st) < 0) {
        printf("cp: %s: No such file\n", argv[1]);
        return 1;
    }
    if (st.type == 2) {
        printf("cp: %s: Is a directory\n", argv[1]);
        return 1;
    }

    int src = (int)uopen(argv[1], O_RDONLY);
    if (src < 0) {
        printf("cp: cannot open '%s'\n", argv[1]);
        return 1;
    }

    int dst = (int)uopen(argv[2], O_CREATE | O_TRUNC);
    if (dst < 0) {
        printf("cp: cannot create '%s'\n", argv[2]);
        uclose(src);
        return 1;
    }

    char buf[512];
    int n;
    while ((n = (int)uread(src, buf, sizeof(buf))) > 0) {
        uwrite(dst, buf, n);
    }

    uclose(src);
    uclose(dst);
    return 0;
}
