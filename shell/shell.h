#ifndef SHELL_SHELL_H
#define SHELL_SHELL_H

void shell_init(void);
void shell_task_entry(void);
void print_prompt(void);

/* Execute a single command line (with pipe/redirection/var expansion).
 * Used by the shell loop and by cmd_run for script execution.
 * Returns the exit code (also stored in last_exit_code). */
int shell_execute_line(const char *line);

/* Last command exit code ($?) */
extern int last_exit_code;

/* Shell-local variable lookup (checked before env_get) */
const char *shell_var_get(const char *name);
void shell_var_set(const char *name, const char *value);
void shell_var_unset(const char *name);

/* Iterate shell vars: returns 0 on success, -1 when done */
int shell_var_iter(int index, const char **name, const char **value);

#endif
