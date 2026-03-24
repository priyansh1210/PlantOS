#include "user/libc/ulibc.h"

int main(int argc, char **argv) {
    printf("=== File I/O Demo ===\n\n");

    /* Create and write a file */
    printf("Creating /tmp/test.txt...\n");
    mkdir("/tmp");
    int fd = open("/tmp/test.txt", O_CREATE);
    if (fd < 0) {
        printf("Failed to create file\n");
        return 1;
    }
    const char *msg = "Hello from user space!\nLine 2\nLine 3\n";
    int len = strlen(msg);
    write(fd, msg, len);
    close(fd);
    printf("Wrote %d bytes\n\n", len);

    /* Stat the file */
    struct ustat st;
    if (stat("/tmp/test.txt", &st) == 0) {
        printf("stat: type=%u size=%u\n\n", st.type, st.size);
    }

    /* Read it back */
    printf("Reading /tmp/test.txt:\n");
    fd = open("/tmp/test.txt", O_RDONLY);
    if (fd < 0) {
        printf("Failed to open for read\n");
        return 1;
    }
    char buf[128];
    int n = read(fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }

    /* Test lseek — go back to start and read again */
    printf("\nlseek to start, read first 5 bytes: ");
    lseek(fd, 0, SEEK_SET);
    n = read(fd, buf, 5);
    if (n > 0) {
        buf[n] = '\0';
        printf("'%s'\n", buf);
    }
    close(fd);

    /* Show cwd and env */
    char cwdbuf[128];
    if (getcwd(cwdbuf, sizeof(cwdbuf)))
        printf("\ncwd: %s\n", cwdbuf);

    const char *path = getenv("PATH");
    if (path)
        printf("PATH: %s\n", path);

    printf("\n=== Done ===\n");
    return 0;
}
