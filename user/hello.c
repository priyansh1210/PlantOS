#include "user/libc/ulibc.h"

int main(int argc, char **argv) {
    printf("[HELLO] Hello from ELF user mode! PID=%lu\n", ugetpid());
    printf("[HELLO] argc=%d\n", argc);
    for (int i = 0; i < argc; i++) {
        printf("[HELLO] argv[%d]=%s\n", i, argv[i]);
    }
    printf("[HELLO] Exiting.\n");
    return 0;
}
