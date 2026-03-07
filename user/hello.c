#include "user/libc/ulibc.h"

int main(void) {
    printf("[HELLO] Hello from ELF user mode! PID=%lu\n", ugetpid());
    printf("[HELLO] Exiting.\n");
    return 0;
}
