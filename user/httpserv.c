#include "user/usyscall.h"
#include "user/libc/ulibc.h"

/*
 * PlantOS HTTP Server
 * Listens on a configurable port (default 80), serves simple status pages.
 * Uses TCP sockets: ulisten / uaccept / urecv / usend / usockclose
 */

static int str_starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s++ != *prefix++) return 0;
    }
    return 1;
}

static int str_len(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

/* Build an HTTP response into buf. Returns total length. */
static int http_response(char *buf, int bufsize, const char *status,
                         const char *content_type, const char *body) {
    int pos = 0;
    int body_len = str_len(body);

    /* Status line */
    const char *s = "HTTP/1.0 ";
    while (*s && pos < bufsize) buf[pos++] = *s++;
    s = status;
    while (*s && pos < bufsize) buf[pos++] = *s++;
    buf[pos++] = '\r'; buf[pos++] = '\n';

    /* Content-Type */
    s = "Content-Type: ";
    while (*s && pos < bufsize) buf[pos++] = *s++;
    s = content_type;
    while (*s && pos < bufsize) buf[pos++] = *s++;
    buf[pos++] = '\r'; buf[pos++] = '\n';

    /* Content-Length (simple int to string) */
    s = "Content-Length: ";
    while (*s && pos < bufsize) buf[pos++] = *s++;
    char num[16];
    int ni = 0;
    int tmp = body_len;
    if (tmp == 0) num[ni++] = '0';
    else {
        char rev[16];
        int ri = 0;
        while (tmp > 0) { rev[ri++] = '0' + (tmp % 10); tmp /= 10; }
        while (ri > 0) num[ni++] = rev[--ri];
    }
    for (int i = 0; i < ni && pos < bufsize; i++) buf[pos++] = num[i];
    buf[pos++] = '\r'; buf[pos++] = '\n';

    /* Connection: close */
    s = "Connection: close\r\n";
    while (*s && pos < bufsize) buf[pos++] = *s++;

    /* End of headers */
    buf[pos++] = '\r'; buf[pos++] = '\n';

    /* Body */
    for (int i = 0; i < body_len && pos < bufsize; i++)
        buf[pos++] = body[i];

    return pos;
}

/* Serve the index page */
static const char *page_index =
    "<html><head><title>PlantOS</title></head>"
    "<body style='font-family:monospace;background:#111;color:#0f0;padding:20px'>"
    "<h1>PlantOS HTTP Server</h1>"
    "<p>A hobby x86_64 operating system serving web pages!</p>"
    "<ul>"
    "<li><a href='/status' style='color:#0ff'>/status</a> - System status</li>"
    "<li><a href='/about' style='color:#0ff'>/about</a> - About PlantOS</li>"
    "</ul>"
    "<hr><small>Powered by PlantOS TCP/IP stack</small>"
    "</body></html>";

static const char *page_about =
    "<html><head><title>About PlantOS</title></head>"
    "<body style='font-family:monospace;background:#111;color:#0f0;padding:20px'>"
    "<h1>About PlantOS</h1>"
    "<p>PlantOS is an x86_64 operating system built from scratch.</p>"
    "<p>Features: preemptive multitasking, virtual memory, FAT32 filesystem,<br>"
    "TCP/IP networking, DNS client, window manager, neural network engine.</p>"
    "<p><a href='/' style='color:#0ff'>Back to home</a></p>"
    "</body></html>";

static const char *page_status =
    "<html><head><title>PlantOS Status</title></head>"
    "<body style='font-family:monospace;background:#111;color:#0f0;padding:20px'>"
    "<h1>System Status</h1>"
    "<p>HTTP Server: Running</p>"
    "<p>Network: E1000 NIC, TCP/IP, DNS</p>"
    "<p>Neural Network Engine: Available (sigmoid, tanh, relu, softmax)</p>"
    "<p><a href='/' style='color:#0ff'>Back to home</a></p>"
    "</body></html>";

static const char *page_404 =
    "<html><head><title>404</title></head>"
    "<body style='font-family:monospace;background:#111;color:#f00;padding:20px'>"
    "<h1>404 Not Found</h1>"
    "<p><a href='/' style='color:#0ff'>Back to home</a></p>"
    "</body></html>";

static void handle_request(int conn, const char *request) {
    char resp[2048];
    int len;

    if (str_starts_with(request, "GET / ") || str_starts_with(request, "GET / HTTP")) {
        len = http_response(resp, sizeof(resp), "200 OK", "text/html", page_index);
    } else if (str_starts_with(request, "GET /about")) {
        len = http_response(resp, sizeof(resp), "200 OK", "text/html", page_about);
    } else if (str_starts_with(request, "GET /status")) {
        len = http_response(resp, sizeof(resp), "200 OK", "text/html", page_status);
    } else {
        len = http_response(resp, sizeof(resp), "404 Not Found", "text/html", page_404);
    }

    usend(conn, resp, (uint16_t)len);
}

int main(int argc, char **argv) {
    uint16_t port = 80;

    if (argc >= 2) {
        port = 0;
        for (int i = 0; argv[1][i]; i++)
            port = port * 10 + (argv[1][i] - '0');
    }

    printf("PlantOS HTTP Server starting on port %d\n", port);

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

        /* Read request */
        char req[512];
        int n = (int)urecv(conn, req, sizeof(req) - 1);
        if (n > 0) {
            req[n] = '\0';
            /* Extract first line for logging */
            char first_line[80];
            int fl = 0;
            while (fl < n && fl < 79 && req[fl] != '\r' && req[fl] != '\n')
                { first_line[fl] = req[fl]; fl++; }
            first_line[fl] = '\0';
            printf("[%d] %s\n", count, first_line);

            handle_request(conn, req);
        }

        usockclose(conn);
    }

    return 0;
}
