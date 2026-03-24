#include "shell/shell.h"
#include "shell/commands.h"
#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "lib/printf.h"
#include "lib/string.h"
#include "lib/util.h"
#include "kernel/env.h"
#include "fs/vfs.h"
#include "task/task.h"

/* ---- Last exit code ---- */

int last_exit_code = 0;

/* ---- Shell-local variables ---- */

#define SHELL_VAR_MAX 32
#define SHELL_VAR_NAME_MAX 32
#define SHELL_VAR_VAL_MAX 128

static struct {
    char name[SHELL_VAR_NAME_MAX];
    char value[SHELL_VAR_VAL_MAX];
    int  used;
} shell_vars[SHELL_VAR_MAX];

const char *shell_var_get(const char *name) {
    for (int i = 0; i < SHELL_VAR_MAX; i++) {
        if (shell_vars[i].used && strcmp(shell_vars[i].name, name) == 0)
            return shell_vars[i].value;
    }
    return NULL;
}

void shell_var_set(const char *name, const char *value) {
    /* Update existing */
    for (int i = 0; i < SHELL_VAR_MAX; i++) {
        if (shell_vars[i].used && strcmp(shell_vars[i].name, name) == 0) {
            strncpy(shell_vars[i].value, value, SHELL_VAR_VAL_MAX - 1);
            shell_vars[i].value[SHELL_VAR_VAL_MAX - 1] = '\0';
            return;
        }
    }
    /* Add new */
    for (int i = 0; i < SHELL_VAR_MAX; i++) {
        if (!shell_vars[i].used) {
            shell_vars[i].used = 1;
            strncpy(shell_vars[i].name, name, SHELL_VAR_NAME_MAX - 1);
            shell_vars[i].name[SHELL_VAR_NAME_MAX - 1] = '\0';
            strncpy(shell_vars[i].value, value, SHELL_VAR_VAL_MAX - 1);
            shell_vars[i].value[SHELL_VAR_VAL_MAX - 1] = '\0';
            return;
        }
    }
}

void shell_var_unset(const char *name) {
    for (int i = 0; i < SHELL_VAR_MAX; i++) {
        if (shell_vars[i].used && strcmp(shell_vars[i].name, name) == 0) {
            shell_vars[i].used = 0;
            return;
        }
    }
}

int shell_var_iter(int index, const char **name, const char **value) {
    int count = 0;
    for (int i = 0; i < SHELL_VAR_MAX; i++) {
        if (shell_vars[i].used) {
            if (count == index) {
                *name = shell_vars[i].name;
                *value = shell_vars[i].value;
                return 0;
            }
            count++;
        }
    }
    return -1;
}

/* ---- $VAR expansion ---- */

/* Expand $VAR references in a command line. Handles $VAR, ${VAR}, and $?.
 * Writes result into out (up to outsize-1 chars). */
static void expand_vars(const char *in, char *out, int outsize) {
    int o = 0;
    for (int i = 0; in[i] && o < outsize - 1; ) {
        if (in[i] == '$') {
            i++;

            /* Special: $? = last exit code */
            if (in[i] == '?') {
                i++;
                char num[12];
                int n = last_exit_code;
                int neg = 0;
                if (n < 0) { neg = 1; n = -n; }
                int pos = 0;
                do { num[pos++] = '0' + (n % 10); n /= 10; } while (n > 0);
                if (neg && o < outsize - 1) out[o++] = '-';
                for (int j = pos - 1; j >= 0 && o < outsize - 1; j--)
                    out[o++] = num[j];
                continue;
            }

            /* Arithmetic expansion: $((expr)) */
            if (in[i] == '(' && in[i+1] == '(') {
                i += 2;
                /* Read expression up to )) */
                char expr[128];
                int ei = 0;
                while (in[i] && ei < 127) {
                    if (in[i] == ')' && in[i+1] == ')') { i += 2; break; }
                    expr[ei++] = in[i++];
                }
                expr[ei] = '\0';

                /* Simple arithmetic: parse A OP B or just A */
                /* First expand any vars in the expression */
                char eexp[128];
                expand_vars(expr, eexp, 128);

                /* Parse: skip spaces, read number, skip spaces, read op, skip spaces, read number */
                char *p = eexp;
                while (*p == ' ') p++;
                int neg1 = 0;
                if (*p == '-') { neg1 = 1; p++; }
                int a = 0;
                while (*p >= '0' && *p <= '9') a = a * 10 + (*p++ - '0');
                if (neg1) a = -a;
                while (*p == ' ') p++;

                int result = a;
                if (*p) {
                    char op = *p++;
                    while (*p == ' ') p++;
                    int neg2 = 0;
                    if (*p == '-') { neg2 = 1; p++; }
                    int b = 0;
                    while (*p >= '0' && *p <= '9') b = b * 10 + (*p++ - '0');
                    if (neg2) b = -b;

                    if (op == '+') result = a + b;
                    else if (op == '-') result = a - b;
                    else if (op == '*') result = a * b;
                    else if (op == '/' && b != 0) result = a / b;
                    else if (op == '%' && b != 0) result = a % b;
                }

                /* Write result */
                char num[12];
                int rn = result;
                int rneg = 0;
                if (rn < 0) { rneg = 1; rn = -rn; }
                int pos = 0;
                do { num[pos++] = '0' + (rn % 10); rn /= 10; } while (rn > 0);
                if (rneg && o < outsize - 1) out[o++] = '-';
                for (int j = pos - 1; j >= 0 && o < outsize - 1; j--)
                    out[o++] = num[j];
                continue;
            }

            /* Check for ${VAR} syntax */
            int brace = 0;
            if (in[i] == '{') { brace = 1; i++; }

            /* Read variable name (alphanumeric + underscore) */
            char varname[64];
            int vn = 0;
            while (in[i] && vn < 63) {
                if (brace) {
                    if (in[i] == '}') { i++; break; }
                } else {
                    char c = in[i];
                    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                          (c >= '0' && c <= '9') || c == '_'))
                        break;
                }
                varname[vn++] = in[i++];
            }
            varname[vn] = '\0';

            if (vn > 0) {
                /* Check shell-local vars first, then environment */
                const char *val = shell_var_get(varname);
                if (!val) val = env_get(varname);
                if (val) {
                    while (*val && o < outsize - 1)
                        out[o++] = *val++;
                }
            } else {
                /* Lone $ — copy literally */
                out[o++] = '$';
            }
        } else {
            out[o++] = in[i++];
        }
    }
    out[o] = '\0';
}

#define INPUT_MAX 256

/* ---- Command history ---- */
#define HISTORY_SIZE 16

static char history[HISTORY_SIZE][INPUT_MAX];
static int  history_count = 0;
static int  history_write = 0;  /* Next slot to write */
static int  history_browse = -1; /* -1 = not browsing */

static char input_buf[INPUT_MAX];
static char saved_buf[INPUT_MAX]; /* Saved current line when browsing history */
static int  input_pos = 0;

/* Deferred command execution — commands must not run inside IRQ handler
 * because some (like the editor) enter their own hlt loop. */
static volatile bool command_pending = false;

static void history_add(const char *line) {
    if (strlen(line) == 0) return;
    /* Don't add if same as last entry */
    int prev = (history_write - 1 + HISTORY_SIZE) % HISTORY_SIZE;
    if (history_count > 0 && strcmp(history[prev], line) == 0) return;

    strcpy(history[history_write], line);
    history_write = (history_write + 1) % HISTORY_SIZE;
    if (history_count < HISTORY_SIZE) history_count++;
}

/* ---- Line editing helpers ---- */

static void clear_input_line(void) {
    /* Erase current input from display */
    while (input_pos > 0) {
        vga_backspace();
        input_pos--;
    }
}

static void redisplay_input(void) {
    for (int i = 0; i < input_pos; i++)
        kprintf("%c", input_buf[i]);
}

static void set_input(const char *s) {
    clear_input_line();
    int len = strlen(s);
    if (len >= INPUT_MAX) len = INPUT_MAX - 1;
    memcpy(input_buf, s, len);
    input_buf[len] = '\0';
    input_pos = len;
    redisplay_input();
}

/* ---- Tab completion ---- */

static void tab_complete(void) {
    if (input_pos == 0) return;

    input_buf[input_pos] = '\0';

    /* Only complete the first token (command name) if no space yet */
    char *space = NULL;
    for (int i = 0; i < input_pos; i++) {
        if (input_buf[i] == ' ') { space = &input_buf[i]; break; }
    }

    if (!space) {
        /* Complete command name */
        const char *match = NULL;
        int match_count = 0;
        int prefix_len = input_pos;

        for (int i = 0; ; i++) {
            const char *name = commands_get_name(i);
            if (!name) break;
            if (strncmp(name, input_buf, prefix_len) == 0) {
                match = name;
                match_count++;
            }
        }

        if (match_count == 1) {
            /* Single match — complete it */
            set_input(match);
            /* Add a trailing space */
            if (input_pos < INPUT_MAX - 1) {
                input_buf[input_pos++] = ' ';
                kprintf(" ");
            }
        } else if (match_count > 1) {
            /* Multiple matches — show them */
            kprintf("\n");
            for (int i = 0; ; i++) {
                const char *name = commands_get_name(i);
                if (!name) break;
                if (strncmp(name, input_buf, prefix_len) == 0)
                    kprintf("  %s", name);
            }
            kprintf("\n");
            print_prompt();
            redisplay_input();
        }
    } else {
        /* Complete file/directory name for the current argument */
        char *arg_start = space + 1;
        /* Find start of last argument */
        for (int i = input_pos - 1; i > 0; i--) {
            if (input_buf[i] == ' ') { arg_start = &input_buf[i + 1]; break; }
        }
        int arg_len = (int)(&input_buf[input_pos] - arg_start);
        if (arg_len <= 0) return;

        /* Resolve the directory part */
        char dir_path[VFS_PATH_MAX];
        char prefix[64];
        int last_slash = -1;
        for (int i = 0; i < arg_len; i++) {
            if (arg_start[i] == '/') last_slash = i;
        }

        if (last_slash >= 0) {
            /* Has directory component */
            memcpy(prefix, arg_start + last_slash + 1, arg_len - last_slash - 1);
            prefix[arg_len - last_slash - 1] = '\0';
            char partial_dir[VFS_PATH_MAX];
            memcpy(partial_dir, arg_start, last_slash + 1);
            partial_dir[last_slash + 1] = '\0';
            path_resolve(partial_dir, dir_path, sizeof(dir_path));
        } else {
            /* No directory — use cwd */
            path_resolve(".", dir_path, sizeof(dir_path));
            memcpy(prefix, arg_start, arg_len);
            prefix[arg_len] = '\0';
        }

        int prefix_len2 = strlen(prefix);
        int fd = vfs_open(dir_path, 0);
        if (fd < 0) return;

        const char *match = NULL;
        int match_count = 0;
        static char match_buf[VFS_NAME_MAX];
        struct vfs_dirent de;

        while (vfs_readdir(fd, &de) == 0) {
            if (strncmp(de.name, prefix, prefix_len2) == 0) {
                strcpy(match_buf, de.name);
                match = match_buf;
                match_count++;
            }
        }
        vfs_close(fd);

        if (match_count == 1) {
            /* Complete the filename */
            /* Replace the argument part */
            int new_arg_len;
            char new_arg[VFS_PATH_MAX];
            if (last_slash >= 0) {
                memcpy(new_arg, arg_start, last_slash + 1);
                strcpy(new_arg + last_slash + 1, match);
                new_arg_len = last_slash + 1 + strlen(match);
            } else {
                strcpy(new_arg, match);
                new_arg_len = strlen(match);
            }

            /* Rebuild input */
            int before_arg = (int)(arg_start - input_buf);
            char temp[INPUT_MAX];
            memcpy(temp, input_buf, before_arg);
            memcpy(temp + before_arg, new_arg, new_arg_len);
            temp[before_arg + new_arg_len] = '\0';

            clear_input_line();
            strcpy(input_buf, temp);
            input_pos = before_arg + new_arg_len;
            redisplay_input();
        }
    }
}

/* ---- Prompt ---- */

void print_prompt(void) {
    struct task *t = task_current();
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    if (t && t->uid == 0)
        kprintf("root");
    else
        kprintf("user");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    kprintf("@");
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    kprintf("plantos");
    vga_set_color(VGA_CYAN, VGA_BLACK);
    kprintf(":%s", cwd_get());
    vga_set_color(VGA_WHITE, VGA_BLACK);
    kprintf("%c ", (t && t->uid == 0) ? '#' : '$');
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

/* ---- I/O redirection parsing ---- */

/* Parse and strip redirection operators from the token list.
 * Returns modified argc. Sets redir_in, redir_out, redir_append. */
static int parse_redirections(int argc, char **argv,
                               const char **redir_in,
                               const char **redir_out,
                               int *redir_append) {
    *redir_in = NULL;
    *redir_out = NULL;
    *redir_append = 0;

    int new_argc = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "<") == 0 && i + 1 < argc) {
            *redir_in = argv[++i];
        } else if (strcmp(argv[i], ">>") == 0 && i + 1 < argc) {
            *redir_out = argv[++i];
            *redir_append = 1;
        } else if (strcmp(argv[i], ">") == 0 && i + 1 < argc) {
            *redir_out = argv[++i];
            *redir_append = 0;
        } else {
            argv[new_argc++] = argv[i];
        }
    }
    return new_argc;
}

/* ---- Captured output for redirection ---- */

static char *redir_capture_buf = NULL;
static int   redir_capture_len = 0;
static int   redir_capture_cap = 0;

static void redir_capture_char(char c) {
    if (redir_capture_len < redir_capture_cap - 1) {
        redir_capture_buf[redir_capture_len++] = c;
    }
}

/* ---- Command execution with redirection ---- */

static void execute_with_redirection(int argc, char **argv) {
    const char *redir_in = NULL;
    const char *redir_out = NULL;
    int redir_append = 0;

    argc = parse_redirections(argc, argv, &redir_in, &redir_out, &redir_append);
    if (argc == 0) return;

    /* Handle output redirection by capturing kprintf output */
    if (redir_out) {
        static char capture_buf[4096];
        redir_capture_buf = capture_buf;
        redir_capture_len = 0;
        redir_capture_cap = sizeof(capture_buf);

        kprintf_set_capture(redir_capture_char);
        commands_execute(argc, argv);
        kprintf_clear_capture();

        /* Write captured output to file */
        char resolved[256];
        path_resolve(redir_out, resolved, sizeof(resolved));

        if (redir_append) {
            /* Open existing, seek to end, write */
            int fd = vfs_open(resolved, VFS_O_CREATE);
            if (fd >= 0) {
                vfs_lseek(fd, 0, VFS_SEEK_END);
                vfs_write(fd, redir_capture_buf, redir_capture_len);
                vfs_close(fd);
            } else {
                kprintf("shell: cannot open '%s'\n", redir_out);
            }
        } else {
            /* Truncate: create new file */
            int fd = vfs_open(resolved, VFS_O_CREATE);
            if (fd >= 0) {
                vfs_write(fd, redir_capture_buf, redir_capture_len);
                vfs_close(fd);
            } else {
                kprintf("shell: cannot open '%s'\n", redir_out);
            }
        }

        redir_capture_buf = NULL;
    } else if (redir_in) {
        /* Input redirection — not easily supported with current shell model.
         * For now, just run the command normally. */
        commands_execute(argc, argv);
    } else {
        commands_execute(argc, argv);
    }
}

/* ---- Pipe chain execution ---- */

/* Check if the command line contains a pipe character */
static int has_pipe(const char *line) {
    for (int i = 0; line[i]; i++) {
        if (line[i] == '|') return 1;
    }
    return 0;
}

static void execute_pipe_chain(char *line) {
    /* Split line on '|' — execute left side with output capture,
     * then feed output as a temp file and show it */

    /* Find the first pipe */
    char *pipe_pos = NULL;
    for (int i = 0; line[i]; i++) {
        if (line[i] == '|') { pipe_pos = &line[i]; break; }
    }
    if (!pipe_pos) {
        /* No pipe — normal execution */
        char *tokens[16];
        int argc = tokenize(line, tokens, 16);
        if (argc > 0)
            execute_with_redirection(argc, tokens);
        return;
    }

    /* Split: left | right */
    *pipe_pos = '\0';
    char *left = line;
    char *right = pipe_pos + 1;

    /* Skip whitespace */
    while (*right == ' ') right++;

    /* Execute left side with output capture */
    static char pipe_buf[4096];
    redir_capture_buf = pipe_buf;
    redir_capture_len = 0;
    redir_capture_cap = sizeof(pipe_buf);

    char *left_tokens[16];
    int left_argc = tokenize(left, left_tokens, 16);

    if (left_argc > 0) {
        kprintf_set_capture(redir_capture_char);
        commands_execute(left_argc, left_tokens);
        kprintf_clear_capture();
    }

    /* Store left output in a temp file */
    vfs_mkdir("/tmp");
    int fd = vfs_open("/tmp/.pipe", VFS_O_CREATE);
    if (fd >= 0) {
        /* Reset file contents by closing and reopening */
        vfs_close(fd);
        fd = vfs_open("/tmp/.pipe", VFS_O_CREATE);
        if (fd >= 0) {
            vfs_write(fd, pipe_buf, redir_capture_len);
            vfs_close(fd);
        }
    }

    redir_capture_buf = NULL;

    /* Execute right side — if it's cat with no args, feed it the pipe data */
    /* For now, just print the captured output since we can't easily redirect stdin */
    char *right_tokens[16];
    int right_argc = tokenize(right, right_tokens, 16);

    if (right_argc > 0) {
        /* Simple approach: if the right command has no file argument,
         * display the pipe output; otherwise run it normally */
        if (has_pipe(right)) {
            /* Recursive pipe — just show captured output for now */
            for (int i = 0; i < redir_capture_len; i++)
                kprintf("%c", pipe_buf[i]);
        } else {
            execute_with_redirection(right_argc, right_tokens);
        }
    } else {
        /* No right command — just print captured output */
        for (int i = 0; i < redir_capture_len; i++)
            kprintf("%c", pipe_buf[i]);
    }
}

/* ---- Check for VAR=value assignment ---- */

static int is_var_assignment(const char *line) {
    /* Must start with alpha or underscore */
    if (!((*line >= 'A' && *line <= 'Z') || (*line >= 'a' && *line <= 'z') || *line == '_'))
        return 0;
    for (const char *p = line; *p && *p != ' ' && *p != '\t'; p++) {
        if (*p == '=') return 1;
    }
    return 0;
}

static void handle_var_assignment(const char *line) {
    char name[64], value[128];
    int ni = 0;
    const char *p = line;
    while (*p && *p != '=' && ni < 63)
        name[ni++] = *p++;
    name[ni] = '\0';
    if (*p == '=') p++;
    int vi = 0;
    while (*p && vi < 127)
        value[vi++] = *p++;
    value[vi] = '\0';
    shell_var_set(name, value);
}

/* ---- Execute a single line (public API for scripting) ---- */

int shell_execute_line(const char *line) {
    /* Expand $VAR references */
    char expanded[INPUT_MAX];
    expand_vars(line, expanded, INPUT_MAX);

    /* Skip empty lines and comments */
    char *p = expanded;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0' || *p == '#') return last_exit_code;

    /* Check for VAR=value assignment (no spaces before =) */
    if (is_var_assignment(p)) {
        handle_var_assignment(p);
        last_exit_code = 0;
        return 0;
    }

    /* Make a copy for parsing (tokenize modifies the string) */
    char line_copy[INPUT_MAX];
    strcpy(line_copy, expanded);

    if (has_pipe(line_copy)) {
        execute_pipe_chain(line_copy);
    } else {
        char *tokens[16];
        int argc = tokenize(line_copy, tokens, 16);
        if (argc > 0)
            execute_with_redirection(argc, tokens);
    }

    return last_exit_code;
}

/* ---- Process command ---- */

static void process_command(void) {
    input_buf[input_pos] = '\0';

    if (input_pos == 0) {
        print_prompt();
        return;
    }

    /* Add to history */
    history_add(input_buf);
    history_browse = -1;

    shell_execute_line(input_buf);

    input_pos = 0;
    jobs_update();
    print_prompt();
}

/* ---- Key handler ---- */

static void shell_key_handler(char c) {
    if (c == '\n') {
        kprintf("\n");
        command_pending = true;
        return;
    }

    if (c == '\b') {
        if (input_pos > 0) {
            input_pos--;
            vga_backspace();
        }
        return;
    }

    if (c == '\t') {
        input_buf[input_pos] = '\0';
        tab_complete();
        return;
    }

    /* Arrow keys for history */
    unsigned char uc = (unsigned char)c;
    if (uc == KEY_UP) {
        if (history_count == 0) return;
        if (history_browse == -1) {
            /* Save current input */
            input_buf[input_pos] = '\0';
            strcpy(saved_buf, input_buf);
            history_browse = (history_write - 1 + HISTORY_SIZE) % HISTORY_SIZE;
        } else {
            int next = (history_browse - 1 + HISTORY_SIZE) % HISTORY_SIZE;
            /* Don't go past the oldest entry */
            int oldest = (history_write - history_count + HISTORY_SIZE) % HISTORY_SIZE;
            if (history_browse == oldest) return;
            history_browse = next;
        }
        set_input(history[history_browse]);
        return;
    }

    if (uc == KEY_DOWN) {
        if (history_browse == -1) return;
        int newest = (history_write - 1 + HISTORY_SIZE) % HISTORY_SIZE;
        if (history_browse == newest) {
            /* Return to saved input */
            history_browse = -1;
            set_input(saved_buf);
        } else {
            history_browse = (history_browse + 1) % HISTORY_SIZE;
            set_input(history[history_browse]);
        }
        return;
    }

    /* Ignore other special keys */
    if (uc >= 0x80) return;

    /* Regular character */
    if (input_pos < INPUT_MAX - 1) {
        input_buf[input_pos++] = c;
        kprintf("%c", c);
    }
}

/* Task entry point for the shell */
void shell_task_entry(void) {
    commands_init();
    keyboard_set_handler(shell_key_handler);

    kprintf("\n");
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    kprintf("Type 'help' for available commands.\n\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    print_prompt();

    /* Shell main loop — process commands outside IRQ context */
    for (;;) {
        __asm__ volatile ("hlt");
        if (command_pending) {
            command_pending = false;
            process_command();
        }
    }
}

void shell_init(void) {
    /* Legacy — now use shell_task_entry via task_create */
    shell_task_entry();
}
