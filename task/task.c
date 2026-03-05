#include "task/task.h"
#include "mm/heap.h"
#include "lib/string.h"
#include "lib/printf.h"
#include "fs/elf_loader.h"

static struct task task_pool[MAX_TASKS];
static uint64_t   next_pid = 0;

/* Current running task — accessed by sched.c via task_current/task_set_current */
static struct task *current = NULL;

struct task *task_current(void) {
    return current;
}

void task_set_current(struct task *t) {
    current = t;
}

struct task *task_get_list(void) {
    return current;
}

/* Remove a task from the circular linked list */
static void task_unlink(struct task *t) {
    if (t->next == t) {
        return;
    }
    struct task *prev = t;
    while (prev->next != t)
        prev = prev->next;
    prev->next = t->next;
}

/* Allocate a free TCB slot */
static struct task *alloc_task(const char *name) {
    struct task *t = NULL;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (task_pool[i].state == TASK_UNUSED) {
            t = &task_pool[i];
            break;
        }
    }
    if (!t) {
        kprintf("[SCHED] No free task slots!\n");
        return NULL;
    }
    /* Free any deferred kernel stack from a previous user task */
    if (t->kstack_base) {
        kfree(t->kstack_base);
        t->kstack_base = NULL;
    }
    memset(t, 0, sizeof(*t));
    t->pid = next_pid++;
    strncpy(t->name, name, 31);
    t->name[31] = '\0';
    t->state = TASK_READY;
    return t;
}

/* Insert task into circular run ring */
static void task_insert(struct task *t) {
    if (current) {
        t->next = current->next;
        current->next = t;
    } else {
        t->next = t;
    }
}

struct task *task_create(const char *name, task_entry_t entry) {
    struct task *t = alloc_task(name);
    if (!t) return NULL;

    if (entry == NULL) {
        /* Special case: kernel bootstrap task — already running, no stack setup */
        t->stack_base = NULL;
        t->rsp = 0;
        task_insert(t);
        return t;
    }

    /* Allocate stack */
    t->stack_base = (uint64_t *)kmalloc(TASK_STACK_SIZE);
    if (!t->stack_base) {
        kprintf("[SCHED] Failed to allocate stack for '%s'\n", name);
        t->state = TASK_UNUSED;
        return NULL;
    }
    memset(t->stack_base, 0, TASK_STACK_SIZE);

    /* Point to top of stack */
    uint64_t *sp = (uint64_t *)((uint8_t *)t->stack_base + TASK_STACK_SIZE);

    /*
     * Build fake IRQ frame so the first context switch via iretq
     * lands at entry() with interrupts enabled.
     *
     * Stack layout (matches irq_common_stub pop order):
     *   ss, rsp, rflags, cs, rip   (iretq frame — pushed last, popped first by CPU)
     *   err_code, int_no           (dummy)
     *   rax, rbx, rcx, rdx, rsi, rdi, rbp, r8..r15  (GP regs)
     */
    *(--sp) = 0x10;                        /* ss */
    *(--sp) = (uint64_t)((uint8_t *)t->stack_base + TASK_STACK_SIZE); /* rsp */
    *(--sp) = 0x200;                       /* rflags: IF=1 */
    *(--sp) = 0x08;                        /* cs */
    *(--sp) = (uint64_t)entry;             /* rip */
    *(--sp) = 0;                           /* err_code */
    *(--sp) = 0;                           /* int_no */
    *(--sp) = 0; /* rax */
    *(--sp) = 0; /* rbx */
    *(--sp) = 0; /* rcx */
    *(--sp) = 0; /* rdx */
    *(--sp) = 0; /* rsi */
    *(--sp) = 0; /* rdi */
    *(--sp) = 0; /* rbp */
    *(--sp) = 0; /* r8  */
    *(--sp) = 0; /* r9  */
    *(--sp) = 0; /* r10 */
    *(--sp) = 0; /* r11 */
    *(--sp) = 0; /* r12 */
    *(--sp) = 0; /* r13 */
    *(--sp) = 0; /* r14 */
    *(--sp) = 0; /* r15 */

    t->rsp = (uint64_t)sp;
    task_insert(t);

    kprintf("[SCHED] Created task '%s' (pid %llu)\n", t->name, t->pid);
    return t;
}

struct task *task_create_user(const char *name, task_entry_t entry) {
    struct task *t = alloc_task(name);
    if (!t) return NULL;

    t->is_user = 1;

    /* Allocate user stack */
    t->stack_base = (uint64_t *)kmalloc(TASK_STACK_SIZE);
    if (!t->stack_base) {
        kprintf("[SCHED] Failed to allocate user stack for '%s'\n", name);
        t->state = TASK_UNUSED;
        return NULL;
    }
    memset(t->stack_base, 0, TASK_STACK_SIZE);

    /* Allocate kernel stack (for ring 3→0 transitions via TSS.rsp0) */
    t->kstack_base = (uint64_t *)kmalloc(TASK_STACK_SIZE);
    if (!t->kstack_base) {
        kprintf("[SCHED] Failed to allocate kernel stack for '%s'\n", name);
        kfree(t->stack_base);
        t->state = TASK_UNUSED;
        return NULL;
    }
    memset(t->kstack_base, 0, TASK_STACK_SIZE);
    t->kstack_top = (uint64_t)((uint8_t *)t->kstack_base + TASK_STACK_SIZE);

    /*
     * Build initial frame on the KERNEL stack.
     * The iretq will pop ss/rsp/rflags/cs/rip to enter ring 3.
     * The scheduler will mov rsp to this frame and pop regs + iretq.
     */
    uint64_t *sp = (uint64_t *)t->kstack_top;

    /* iretq frame — ring 3 */
    *(--sp) = 0x23;                        /* ss: user data (0x20 | RPL=3) */
    *(--sp) = (uint64_t)((uint8_t *)t->stack_base + TASK_STACK_SIZE); /* rsp: user stack top */
    *(--sp) = 0x200;                       /* rflags: IF=1 */
    *(--sp) = 0x1B;                        /* cs: user code (0x18 | RPL=3) */
    *(--sp) = (uint64_t)entry;             /* rip: user entry point */
    *(--sp) = 0;                           /* err_code */
    *(--sp) = 0;                           /* int_no */
    *(--sp) = 0; /* rax */
    *(--sp) = 0; /* rbx */
    *(--sp) = 0; /* rcx */
    *(--sp) = 0; /* rdx */
    *(--sp) = 0; /* rsi */
    *(--sp) = 0; /* rdi */
    *(--sp) = 0; /* rbp */
    *(--sp) = 0; /* r8  */
    *(--sp) = 0; /* r9  */
    *(--sp) = 0; /* r10 */
    *(--sp) = 0; /* r11 */
    *(--sp) = 0; /* r12 */
    *(--sp) = 0; /* r13 */
    *(--sp) = 0; /* r14 */
    *(--sp) = 0; /* r15 */

    t->rsp = (uint64_t)sp;
    task_insert(t);

    kprintf("[SCHED] Created user task '%s' (pid %llu)\n", t->name, t->pid);
    return t;
}

struct task *task_create_user_elf(const char *name, uint64_t entry,
                                  uint64_t elf_base, uint64_t elf_npages) {
    struct task *t = task_create_user(name, (task_entry_t)entry);
    if (!t) return NULL;

    t->elf_load_base = elf_base;
    t->elf_num_pages = elf_npages;
    return t;
}

void task_yield(void) {
    struct task *cur = current;
    if (!cur || !cur->next)
        return;

    /* Find next READY task */
    struct task *next = cur->next;
    struct task *start = next;
    do {
        if (next->state == TASK_READY) break;
        next = next->next;
    } while (next != start);

    if (next == cur || next->state != TASK_READY)
        return;

    cur->state = TASK_READY;
    next->state = TASK_RUNNING;
    current = next;
    task_switch_to(&cur->rsp, next->rsp);
}

void task_exit(void) {
    struct task *t = current;

    /*
     * Unlink from run ring and mark unused.
     * Do NOT free stacks here — we may still be running on them
     * (especially kstack for user tasks called from INT 0x80).
     * Stacks are freed lazily when alloc_task() reuses this TCB slot.
     *
     * Do NOT set current = t->next — let the scheduler handle it.
     * t->next still points into the valid ring, so schedule_from_irq()
     * can traverse from it to find the next READY task.
     */
    /* Free ELF pages if this was an ELF task */
    if (t->elf_num_pages > 0) {
        elf_unload(t->elf_load_base, t->elf_num_pages);
        t->elf_load_base = 0;
        t->elf_num_pages = 0;
    }

    task_unlink(t);
    t->state = TASK_UNUSED;

    /* Wait for timer IRQ to preempt us into another task */
    __asm__ volatile ("sti");
    for (;;)
        __asm__ volatile ("hlt");
}

void task_kill(uint64_t pid) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (task_pool[i].pid == pid &&
            task_pool[i].state != TASK_UNUSED) {
            struct task *t = &task_pool[i];

            if (t == current) {
                kprintf("Cannot kill current task\n");
                return;
            }
            if (pid == 0) {
                kprintf("Cannot kill kernel task\n");
                return;
            }

            /* Free ELF pages */
            if (t->elf_num_pages > 0) {
                elf_unload(t->elf_load_base, t->elf_num_pages);
                t->elf_load_base = 0;
                t->elf_num_pages = 0;
            }
            task_unlink(t);
            if (t->stack_base) {
                kfree(t->stack_base);
                t->stack_base = NULL;
            }
            if (t->kstack_base) {
                kfree(t->kstack_base);
                t->kstack_base = NULL;
            }
            t->state = TASK_UNUSED;
            kprintf("Killed task '%s' (pid %llu)\n", t->name, t->pid);
            return;
        }
    }
    kprintf("No task with pid %llu\n", pid);
}
