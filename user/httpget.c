#include "user/usyscall.h"
#include "user/libc/ulibc.h"

int main(int argc, char **argv) {
    const char *host = "10.0.2.2";
    uint16_t port = 8080;
    const char *path = "/";

    if (argc >= 2) host = argv[1];
    if (argc >= 3) {
        port = 0;
        for (int i = 0; argv[2][i]; i++)
            port = port * 10 + (argv[2][i] - '0');
    }
    if (argc >= 4) path = argv[3];

    /* Resolve hostname (handles both IPs and hostnames via DNS) */
    uint32_t ip = 0;
    printf("Resolving %s ...\n", host);
    if (uresolve(host, &ip) < 0) {
        printf("Failed to resolve '%s'\n", host);
        uexit(1);
    }
    printf("Connecting to %s:%d ...\n", host, port);

    int conn = (int)uconnect(ip, port);
    if (conn < 0) {
        printf("Connection failed\n");
        uexit(1);
    }
    printf("Connected (conn=%d)\n", conn);

    /* Build HTTP request */
    char req[256];
    int rlen = 0;
    const char *method = "GET ";
    for (int i = 0; method[i]; i++) req[rlen++] = method[i];
    for (int i = 0; path[i]; i++) req[rlen++] = path[i];
    const char *suffix = " HTTP/1.0\r\nHost: ";
    for (int i = 0; suffix[i]; i++) req[rlen++] = suffix[i];
    for (int i = 0; host[i]; i++) req[rlen++] = host[i];
    const char *end = "\r\n\r\n";
    for (int i = 0; end[i]; i++) req[rlen++] = end[i];
    req[rlen] = '\0';

    int sent = (int)usend(conn, req, (uint16_t)rlen);
    printf("Sent %d bytes\n", sent);

    /* Receive response */
    char buf[512];
    int total = 0;
    while (1) {
        int n = (int)urecv(conn, buf, sizeof(buf) - 1);
        if (n <= 0) break;
        buf[n] = '\0';
        printf("%s", buf);
        total += n;
        if (total > 4096) {
            printf("\n... (truncated)\n");
            break;
        }
    }

    printf("\n--- %d bytes received ---\n", total);
    usockclose(conn);
    uexit(0);
    return 0;
}
