#ifndef SHELL_COMMANDS_H
#define SHELL_COMMANDS_H

void commands_init(void);
void commands_execute(int argc, char **argv);
void jobs_update(void);
const char *commands_get_name(int index);

#endif
