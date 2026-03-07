#include "user/libc/ulibc.h"

static void sigusr1_handler(int signum) {
    (void)signum;
    printf("[SIGDEMO] Got signal SIGUSR1!\n");
    usigreturn();
}

int main(void) {
    uint64_t pid = ugetpid();

    printf("[SIGDEMO] PID=%lu, registering SIGUSR1 handler\n", pid);

    usignal(3, sigusr1_handler); /* SIGUSR1 = 3 */

    for (int i = 0; i < 50; i++) {
        printf("[SIGDEMO] tick %d (PID=%lu)\n", i, pid);
        for (int j = 0; j < 10; j++)
            uyield();
    }

    printf("[SIGDEMO] Exiting.\n");
    return 0;
}
