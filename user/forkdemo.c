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

int main(void) {
    uprint("[FORKDEMO] Parent PID=");
    uprint_num(ugetpid());
    uprint("\n");

    int64_t pid = ufork();

    if (pid == 0) {
        /* Child process */
        uprint("[FORKDEMO] Child PID=");
        uprint_num(ugetpid());
        uprint(" — Hello from the child!\n");
    } else if (pid > 0) {
        /* Parent process */
        uprint("[FORKDEMO] Forked child PID=");
        uprint_num((uint64_t)pid);
        uprint("\n");

        /* Wait for child to finish */
        uwaitpid((uint64_t)pid);
        uprint("[FORKDEMO] Child exited. Parent PID=");
        uprint_num(ugetpid());
        uprint(" done.\n");
    } else {
        uprint("[FORKDEMO] fork() failed!\n");
    }

    return 0;
}
