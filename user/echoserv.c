#include "user/usyscall.h"
#include "user/libc/ulibc.h"

/*
 * PlantOS TCP Echo Server
 * Listens on a configurable port (default 7), echoes back whatever it receives.
 * Runs as a daemon (infinite loop).
 */

int main(int argc, char **argv) {
    uint16_t port = 7;

    if (argc >= 2) {
        port = 0;
        for (int i = 0; argv[1][i]; i++)
            port = port * 10 + (argv[1][i] - '0');
    }

    printf("PlantOS Echo Server starting on port %d\n", port);

    int listener = (int)ulisten(port);
    if (listener < 0) {
        printf("Failed to listen on port %d\n", port);
        uexit(1);
    }
    printf("Listening on port %d (daemon mode)...\n", port);

    int count = 0;
    for (;;) {
        int conn = (int)uaccept(listener);
        if (conn < 0) {
            uyield();
            continue;
        }

        count++;
        printf("[echo %d] Connection accepted\n", count);

        /* Echo loop: read and send back */
        char buf[512];
        int n;
        int total = 0;
        while ((n = (int)urecv(conn, buf, sizeof(buf))) > 0) {
            usend(conn, buf, (uint16_t)n);
            total += n;
        }

        printf("[echo %d] Done (%d bytes echoed)\n", count, total);
        usockclose(conn);
    }

    return 0;
}
