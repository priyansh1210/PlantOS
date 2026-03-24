#ifndef TASK_TASK_H
#define TASK_TASK_H

#include <plantos/types.h>
#include "cpu/idt.h"
#include "cpu/fpu.h"
#include "mm/vma.h"

#define MAX_TASKS   64
#define TASK_STACK_SIZE 4096  /* 4 KB per task stack */

/* Per-process file descriptor table */
#define PROC_MAX_FDS    16
#define OFD_NONE        (-1)   /* fd slot unused */
#define OFD_CONSOLE_IN  (-2)   /* stdin: console input */
#define OFD_CONSOLE_OUT (-3)   /* stdout/stderr: console output */

/* User-mode virtual stack address (mapped per-process) */
#define USER_STACK_VADDR  0x7FF000
#define USER_STACK_TOP    0x800000

typedef enum {
    TASK_UNUSED = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_ZOMBIE,
    TASK_BLOCKED,
    TASK_STOPPED
} task_state_t;

typedef void (*task_entry_t)(void);
typedef void (*kthread_fn_t)(void *arg);

struct task {
    uint64_t      rsp;           /* Saved stack pointer — MUST be offset 0 */
    uint64_t      pid;
    uint64_t      ppid;          /* Parent PID */
    uint64_t      pgid;          /* Process group ID (for job control) */
    uint16_t      uid;           /* User ID (0 = root) */
    uint16_t      gid;           /* Group ID (0 = root) */
    char          name[32];
    task_state_t  state;
    uint64_t      *stack_base;   /* Base of allocated stack (for kfree) */
    uint64_t      ticks;         /* CPU time consumed */
    struct task   *next;         /* Circular linked list */
    /* User-mode fields */
    uint64_t      *kstack_base;  /* Kernel stack base (for user tasks) */
    uint64_t      kstack_top;    /* Kernel stack top (for TSS.rsp0) */
    uint8_t       is_user;       /* 1 if ring 3 task */
    /* Per-process address space */
    uint64_t      cr3;           /* PML4 physical address (0 = use boot CR3) */
    /* ELF loader fields */
    uint64_t      elf_load_base; /* Base vaddr of loaded ELF pages */
    uint64_t      elf_num_pages; /* Number of 4KB pages to free on exit */
    /* User heap (sbrk) */
    uint64_t      brk;           /* Current program break (heap end) */
    /* Signal fields */
    uint32_t      pending_signals;     /* Bitmask of pending signals */
    uint64_t      signal_handlers[12]; /* Handler addresses (0 = default) */
    /* Exit code */
    int           exit_code;
    /* Blocking fields */
    void         *block_channel;      /* What this task is blocked on */
    /* Virtual memory areas (mmap regions) */
    struct vma    vmas[VMA_MAX_PER_TASK];
    uint64_t      mmap_brk;           /* Next mmap hint address */
    /* Per-process file descriptor table */
    int           ofd[PROC_MAX_FDS];  /* maps process fd → global OFD index or special */
    /* FPU/SSE state (512 bytes, must be 16-byte aligned) */
    uint8_t       fpu_state[FPU_STATE_SIZE] __attribute__((aligned(16)));
    uint8_t       fpu_used;           /* 1 if task has used FPU/SSE */
} __attribute__((aligned(16)));

/* Scheduler API */
void     sched_init(void);
struct task *task_create(const char *name, task_entry_t entry);
struct task *kthread_create(const char *name, kthread_fn_t fn, void *arg);
struct task *task_create_user(const char *name, task_entry_t entry);
struct task *task_create_user_elf(const char *name, uint64_t entry,
                                  uint64_t elf_base, uint64_t elf_npages,
                                  uint64_t cr3,
                                  int argc, char **argv);
void     task_yield(void);
void     task_exit(void);
uint64_t schedule_from_irq(uint64_t old_rsp);

/* Fork: clone current user task. Returns child task (parent gets child PID, child gets 0) */
struct task *task_fork(struct registers *parent_regs);

/* Waitpid: block until child exits. Returns 0 on success, -1 on error */
int task_waitpid(uint64_t child_pid);

/* Current task accessor */
struct task *task_current(void);
void         task_set_current(struct task *t);
struct task *task_get_list(void);
void     task_kill(uint64_t pid);

/* Blocking API */
void     task_block(void *channel);
void     task_wake_one(void *channel);
void     task_wake_all(void *channel);

/* Job control: stop and continue */
void task_stop(struct task *t);
void task_continue(struct task *t);

/* Close all per-process file descriptors */
void task_close_fds(struct task *t);

/* Find task by PID */
struct task *task_find(uint64_t pid);

/* Count active (non-unused, non-zombie) tasks */
int task_active_count(void);

/* Assembly context switch (voluntary) */
extern void task_switch_to(uint64_t *old_rsp, uint64_t new_rsp);

#endif
