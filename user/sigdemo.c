#include "user/usyscall.h"

static uint64_t ustrlen(const char *s) {
    uint64_t len = 0;
    while (s[len]) len++;
    return len;
}

static void uprint(const char *s) {
    uwrite(1, s, ustrlen(s));
}

static void uprint_num(uint64_t n) {
    char buf[20];
    int i = 0;
    if (n == 0) {
        uwrite(1, "0", 1);
        return;
    }
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    char rev[20];
    for (int j = 0; j < i; j++)
        rev[j] = buf[i - 1 - j];
    uwrite(1, rev, i);
}

static void sigusr1_handler(int signum) {
    (void)signum;
    uprint("[SIGDEMO] Got signal SIGUSR1!\n");
    usigreturn();
}

int main(void) {
    uint64_t pid = ugetpid();

    uprint("[SIGDEMO] PID=");
    uprint_num(pid);
    uprint(", registering SIGUSR1 handler\n");

    usignal(3, sigusr1_handler); /* SIGUSR1 = 3 */

    for (int i = 0; i < 50; i++) {
        uprint("[SIGDEMO] tick ");
        uprint_num((uint64_t)i);
        uprint(" (PID=");
        uprint_num(pid);
        uprint(")\n");
        /* Yield several times to give time for signals */
        for (int j = 0; j < 10; j++)
            uyield();
    }

    uprint("[SIGDEMO] Exiting.\n");
    return 0;
}
