#ifndef TASK_TASK_H
#define TASK_TASK_H

#include <plantos/types.h>

#define MAX_TASKS   64
#define TASK_STACK_SIZE 4096  /* 4 KB per task stack */

typedef enum {
    TASK_UNUSED = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_ZOMBIE
} task_state_t;

typedef void (*task_entry_t)(void);

struct task {
    uint64_t      rsp;           /* Saved stack pointer — MUST be offset 0 */
    uint64_t      pid;
    char          name[32];
    task_state_t  state;
    uint64_t      *stack_base;   /* Base of allocated stack (for kfree) */
    uint64_t      ticks;         /* CPU time consumed */
    struct task   *next;         /* Circular linked list */
    /* User-mode fields */
    uint64_t      *kstack_base;  /* Kernel stack base (for user tasks) */
    uint64_t      kstack_top;    /* Kernel stack top (for TSS.rsp0) */
    uint8_t       is_user;       /* 1 if ring 3 task */
    /* ELF loader fields */
    uint64_t      elf_load_base; /* Base vaddr of loaded ELF pages */
    uint64_t      elf_num_pages; /* Number of 4KB pages to free on exit */
};

/* Scheduler API */
void     sched_init(void);
struct task *task_create(const char *name, task_entry_t entry);
struct task *task_create_user(const char *name, task_entry_t entry);
struct task *task_create_user_elf(const char *name, uint64_t entry,
                                  uint64_t elf_base, uint64_t elf_npages);
void     task_yield(void);
void     task_exit(void);
uint64_t schedule_from_irq(uint64_t old_rsp);

/* Current task accessor */
struct task *task_current(void);
void         task_set_current(struct task *t);
struct task *task_get_list(void);
void     task_kill(uint64_t pid);

/* Assembly context switch (voluntary) */
extern void task_switch_to(uint64_t *old_rsp, uint64_t new_rsp);

#endif
