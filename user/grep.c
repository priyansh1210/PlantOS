#include "user/usyscall.h"
#include "user/libc/ulibc.h"

/* Simple substring search (no regex — just literal matching) */
static int strstr_match(const char *haystack, const char *needle, int needle_len) {
    for (int i = 0; haystack[i]; i++) {
        int j;
        for (j = 0; j < needle_len; j++) {
            if (haystack[i + j] != needle[j]) break;
        }
        if (j == needle_len) return 1;
    }
    return 0;
}

/* Case-insensitive version */
static char to_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static int strstr_match_i(const char *haystack, const char *needle, int needle_len) {
    for (int i = 0; haystack[i]; i++) {
        int j;
        for (j = 0; j < needle_len; j++) {
            if (to_lower(haystack[i + j]) != to_lower(needle[j])) break;
        }
        if (j == needle_len) return 1;
    }
    return 0;
}

static void grep_file(const char *pattern, int pat_len, const char *path,
                       int show_name, int ignore_case, int count_only, int line_numbers) {
    struct ustat st;
    if (ufstat(path, &st) < 0) {
        printf("grep: %s: No such file\n", path);
        return;
    }
    if (st.type == 2) {
        printf("grep: %s: Is a directory\n", path);
        return;
    }

    int fd = (int)uopen(path, O_RDONLY);
    if (fd < 0) {
        printf("grep: %s: Cannot open\n", path);
        return;
    }

    /* Read entire file (up to 8KB) */
    char buf[8192];
    int total = 0, n;
    while (total < (int)sizeof(buf) - 1 &&
           (n = (int)uread(fd, buf + total, sizeof(buf) - 1 - total)) > 0) {
        total += n;
    }
    buf[total] = '\0';
    uclose(fd);

    /* Process line by line */
    char line[512];
    int li = 0;
    int lineno = 0;
    int match_count = 0;

    for (int i = 0; i <= total; i++) {
        if (buf[i] == '\n' || buf[i] == '\0') {
            line[li] = '\0';
            lineno++;

            int matched = ignore_case
                ? strstr_match_i(line, pattern, pat_len)
                : strstr_match(line, pattern, pat_len);

            if (matched) {
                match_count++;
                if (!count_only) {
                    if (show_name) printf("%s:", path);
                    if (line_numbers) printf("%d:", lineno);
                    printf("%s\n", line);
                }
            }

            li = 0;
        } else if (li < 511) {
            line[li++] = buf[i];
        }
    }

    if (count_only) {
        if (show_name) printf("%s:", path);
        printf("%d\n", match_count);
    }
}

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: grep [-i] [-c] [-n] <pattern> <file> [file2 ...]\n");
        printf("  -i  Case-insensitive\n");
        printf("  -c  Count matches only\n");
        printf("  -n  Show line numbers\n");
        return 1;
    }

    int ignore_case = 0;
    int count_only = 0;
    int line_numbers = 0;
    int first_arg = 1;

    /* Parse flags */
    for (; first_arg < argc; first_arg++) {
        if (argv[first_arg][0] != '-') break;
        for (int j = 1; argv[first_arg][j]; j++) {
            char f = argv[first_arg][j];
            if (f == 'i') ignore_case = 1;
            else if (f == 'c') count_only = 1;
            else if (f == 'n') line_numbers = 1;
        }
    }

    if (first_arg + 1 >= argc) {
        printf("grep: missing pattern or file\n");
        return 1;
    }

    const char *pattern = argv[first_arg];
    int pat_len = strlen(pattern);
    int num_files = argc - first_arg - 1;
    int show_name = (num_files > 1) ? 1 : 0;

    for (int i = first_arg + 1; i < argc; i++) {
        grep_file(pattern, pat_len, argv[i], show_name, ignore_case, count_only, line_numbers);
    }

    return 0;
}
