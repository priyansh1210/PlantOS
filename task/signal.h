#ifndef TASK_SIGNAL_H
#define TASK_SIGNAL_H

#include <plantos/types.h>
#include "cpu/idt.h"
#include "task/task.h"

void signal_init(void);
int  signal_send(uint64_t pid, int signum);
int  signal_register(int signum, uint64_t handler_addr);
void signal_deliver(struct task *t);
void signal_return(struct registers *regs);

#endif
