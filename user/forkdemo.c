#include "user/libc/ulibc.h"

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("[FORKDEMO] Parent PID=%lu\n", ugetpid());

    int64_t pid = ufork();

    if (pid == 0) {
        printf("[FORKDEMO] Child PID=%lu — Hello from the child!\n", ugetpid());
    } else if (pid > 0) {
        printf("[FORKDEMO] Forked child PID=%lu\n", (uint64_t)pid);
        uwaitpid((uint64_t)pid);
        printf("[FORKDEMO] Child exited. Parent PID=%lu done.\n", ugetpid());
    } else {
        printf("[FORKDEMO] fork() failed!\n");
    }

    return 0;
}
