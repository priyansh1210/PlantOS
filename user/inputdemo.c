#include "user/usyscall.h"
#include "user/libc/ulibc.h"

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    printf("=== Interactive Input Demo ===\n");
    printf("Type lines and I'll echo them back.\n");
    printf("Type 'quit' to exit. Ctrl+C to abort. Ctrl+D for EOF.\n\n");

    char buf[128];

    for (;;) {
        printf("> ");

        /* Read a line from stdin */
        int n = (int)uread(STDIN_FILENO, buf, sizeof(buf) - 1);
        if (n <= 0) {
            printf("\n[EOF]\n");
            break;
        }
        buf[n] = '\0';

        /* Strip trailing newline for comparison */
        if (n > 0 && buf[n - 1] == '\n')
            buf[n - 1] = '\0';

        if (strcmp(buf, "quit") == 0) {
            printf("Goodbye!\n");
            break;
        }

        printf("You typed: \"%s\"\n", buf);
    }

    return 0;
}
