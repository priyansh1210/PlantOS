#include "user/usyscall.h"

static uint64_t ustrlen(const char *s) {
    uint64_t len = 0;
    while (s[len]) len++;
    return len;
}

static void uprint(const char *s) {
    uwrite(1, s, ustrlen(s));
}

/* Simple decimal print for small numbers */
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
    /* Reverse */
    char rev[20];
    for (int j = 0; j < i; j++)
        rev[j] = buf[i - 1 - j];
    uwrite(1, rev, i);
}

void user_demo_entry(void) {
    uint64_t pid = ugetpid();

    uprint("[USER] Hello from user mode! PID=");
    uprint_num(pid);
    uprint("\n");

    for (int i = 0; i < 3; i++) {
        uprint("[USER] Tick ");
        uprint_num(i);
        uprint("\n");
        uyield();
    }

    uprint("[USER] Exiting.\n");
    uexit(0);
}
