#include "user/usyscall.h"
#include "user/libc/ulibc.h"

#define MAX_LINES 512
#define MAX_LINE_LEN 256

static char line_pool[MAX_LINES][MAX_LINE_LEN];
static char *lines[MAX_LINES];
static int line_count = 0;

static int read_lines(const char *path) {
    int fd = (int)uopen(path, O_RDONLY);
    if (fd < 0) {
        printf("sort: %s: No such file\n", path);
        return -1;
    }

    char buf[256];
    int n;
    int col = 0;

    while ((n = (int)uread(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) {
            if (buf[i] == '\n' || col >= MAX_LINE_LEN - 1) {
                line_pool[line_count][col] = '\0';
                lines[line_count] = line_pool[line_count];
                line_count++;
                col = 0;
                if (line_count >= MAX_LINES) goto done;
            } else {
                line_pool[line_count][col++] = buf[i];
            }
        }
    }
    /* Handle last line without trailing newline */
    if (col > 0 && line_count < MAX_LINES) {
        line_pool[line_count][col] = '\0';
        lines[line_count] = line_pool[line_count];
        line_count++;
    }
done:
    uclose(fd);
    return 0;
}

int main(int argc, char **argv) {
    int reverse = 0;
    int file_arg = 1;

    if (argc >= 2 && strcmp(argv[1], "-r") == 0) {
        reverse = 1;
        file_arg = 2;
    }

    if (file_arg >= argc) {
        printf("Usage: sort [-r] <file>\n");
        return 1;
    }

    if (read_lines(argv[file_arg]) < 0)
        return 1;

    /* Insertion sort */
    for (int i = 1; i < line_count; i++) {
        char *key = lines[i];
        int j = i - 1;
        while (j >= 0) {
            int cmp = strcmp(lines[j], key);
            if (reverse) cmp = -cmp;
            if (cmp <= 0) break;
            lines[j + 1] = lines[j];
            j--;
        }
        lines[j + 1] = key;
    }

    for (int i = 0; i < line_count; i++)
        printf("%s\n", lines[i]);

    return 0;
}
