#include "task/task.h"
#include "mm/heap.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
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
    t->ppid = current ? current->pid : 0;

    /* Create per-process address space */
    t->cr3 = vmm_create_address_space();
    if (!t->cr3) {
        kprintf("[SCHED] Failed to create address space for '%s'\n", name);
        t->state = TASK_UNUSED;
        return NULL;
    }

    /* Allocate user stack: physical page mapped at USER_STACK_VADDR */
    void *ustack_phys = pmm_alloc_page();
    if (!ustack_phys) {
        vmm_destroy_address_space(t->cr3);
        t->state = TASK_UNUSED;
        return NULL;
    }
    memset(ustack_phys, 0, PAGE_SIZE);
    vmm_map_page_in(t->cr3, USER_STACK_VADDR, (uint64_t)ustack_phys,
                     VMM_PRESENT | VMM_WRITE | VMM_USER);

    /* Allocate kernel stack (for ring 3->0 transitions via TSS.rsp0) */
    t->kstack_base = (uint64_t *)kmalloc(TASK_STACK_SIZE);
    if (!t->kstack_base) {
        vmm_destroy_address_space(t->cr3);
        t->state = TASK_UNUSED;
        return NULL;
    }
    memset(t->kstack_base, 0, TASK_STACK_SIZE);
    t->kstack_top = (uint64_t)((uint8_t *)t->kstack_base + TASK_STACK_SIZE);

    /*
     * Build initial frame on the KERNEL stack.
     * The iretq will pop ss/rsp/rflags/cs/rip to enter ring 3.
     */
    uint64_t *sp = (uint64_t *)t->kstack_top;

    /* iretq frame — ring 3 */
    *(--sp) = 0x23;                        /* ss: user data (0x20 | RPL=3) */
    *(--sp) = USER_STACK_TOP;              /* rsp: top of user stack page */
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

    kprintf("[SCHED] Created user task '%s' (pid %llu, cr3=0x%llx)\n",
            t->name, t->pid, t->cr3);
    return t;
}

struct task *task_create_user_elf(const char *name, uint64_t entry,
                                  uint64_t elf_base, uint64_t elf_npages,
                                  uint64_t cr3) {
    struct task *t = alloc_task(name);
    if (!t) return NULL;

    t->is_user = 1;
    t->ppid = current ? current->pid : 0;
    t->cr3 = cr3;
    t->elf_load_base = elf_base;
    t->elf_num_pages = elf_npages;
    t->brk = elf_base + elf_npages * 4096; /* Heap starts after ELF */

    /* Allocate user stack in the process's address space */
    void *ustack_phys = pmm_alloc_page();
    if (!ustack_phys) {
        t->state = TASK_UNUSED;
        return NULL;
    }
    memset(ustack_phys, 0, PAGE_SIZE);
    vmm_map_page_in(cr3, USER_STACK_VADDR, (uint64_t)ustack_phys,
                     VMM_PRESENT | VMM_WRITE | VMM_USER);

    /* Allocate kernel stack */
    t->kstack_base = (uint64_t *)kmalloc(TASK_STACK_SIZE);
    if (!t->kstack_base) {
        t->state = TASK_UNUSED;
        return NULL;
    }
    memset(t->kstack_base, 0, TASK_STACK_SIZE);
    t->kstack_top = (uint64_t)((uint8_t *)t->kstack_base + TASK_STACK_SIZE);

    /* Build iretq frame on kernel stack */
    uint64_t *sp = (uint64_t *)t->kstack_top;

    *(--sp) = 0x23;                        /* ss */
    *(--sp) = USER_STACK_TOP;              /* rsp */
    *(--sp) = 0x200;                       /* rflags: IF=1 */
    *(--sp) = 0x1B;                        /* cs */
    *(--sp) = entry;                       /* rip: ELF entry point */
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

    kprintf("[SCHED] Created ELF task '%s' (pid %llu, cr3=0x%llx)\n",
            t->name, t->pid, t->cr3);
    return t;
}

struct task *task_fork(struct registers *parent_regs) {
    struct task *parent = current;
    if (!parent || !parent->is_user || !parent->cr3) {
        kprintf("[FORK] Can only fork user tasks\n");
        return NULL;
    }

    struct task *child = alloc_task(parent->name);
    if (!child) return NULL;

    child->is_user = 1;
    child->ppid = parent->pid;

    /* Clone the parent's address space (deep copy of user pages) */
    child->cr3 = vmm_clone_address_space(parent->cr3);
    if (!child->cr3) {
        kprintf("[FORK] Failed to clone address space\n");
        child->state = TASK_UNUSED;
        return NULL;
    }

    /* Copy ELF and heap info */
    child->elf_load_base = parent->elf_load_base;
    child->elf_num_pages = parent->elf_num_pages;
    child->brk = parent->brk;

    /* Allocate kernel stack for child */
    child->kstack_base = (uint64_t *)kmalloc(TASK_STACK_SIZE);
    if (!child->kstack_base) {
        vmm_destroy_address_space(child->cr3);
        child->state = TASK_UNUSED;
        return NULL;
    }
    memset(child->kstack_base, 0, TASK_STACK_SIZE);
    child->kstack_top = (uint64_t)((uint8_t *)child->kstack_base + TASK_STACK_SIZE);

    /*
     * Build the child's kernel stack frame identical to parent's
     * syscall entry frame, but with rax=0 (fork returns 0 to child).
     */
    uint64_t *sp = (uint64_t *)child->kstack_top;

    *(--sp) = parent_regs->ss;
    *(--sp) = parent_regs->rsp;     /* Same user stack vaddr (cloned phys page) */
    *(--sp) = parent_regs->rflags;
    *(--sp) = parent_regs->cs;
    *(--sp) = parent_regs->rip;     /* Resume at same instruction */
    *(--sp) = 0;                    /* err_code */
    *(--sp) = 0;                    /* int_no */
    *(--sp) = 0;                    /* rax = 0 (fork return for child) */
    *(--sp) = parent_regs->rbx;
    *(--sp) = parent_regs->rcx;
    *(--sp) = parent_regs->rdx;
    *(--sp) = parent_regs->rsi;
    *(--sp) = parent_regs->rdi;
    *(--sp) = parent_regs->rbp;
    *(--sp) = parent_regs->r8;
    *(--sp) = parent_regs->r9;
    *(--sp) = parent_regs->r10;
    *(--sp) = parent_regs->r11;
    *(--sp) = parent_regs->r12;
    *(--sp) = parent_regs->r13;
    *(--sp) = parent_regs->r14;
    *(--sp) = parent_regs->r15;

    child->rsp = (uint64_t)sp;
    task_insert(child);

    kprintf("[FORK] Forked pid %llu -> child pid %llu (cr3=0x%llx)\n",
            parent->pid, child->pid, child->cr3);
    return child;
}

int task_waitpid(uint64_t child_pid) {
    struct task *child = task_find(child_pid);
    if (!child) return 0; /* Already exited */
    if (child->ppid != current->pid) return -1; /* Not our child */

    /* Block until child exits */
    task_block(child);
    struct task *cur = current;
    if (cur->state == TASK_BLOCKED) {
        __asm__ volatile ("sti");
        while (cur->state == TASK_BLOCKED)
            __asm__ volatile ("hlt");
    }
    return 0;
}

void task_yield(void) {
    struct task *cur = current;
    if (!cur || !cur->next)
        return;

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

    /* Switch to boot CR3 before destroying our address space */
    if (t->cr3 && t->cr3 != vmm_get_boot_cr3()) {
        uint64_t old_cr3 = t->cr3;
        t->cr3 = 0;
        vmm_switch_address_space(vmm_get_boot_cr3());
        vmm_destroy_address_space(old_cr3);
    }

    /* Wake anyone waiting on us (parent's waitpid) */
    task_wake_all(t);

    task_unlink(t);
    t->state = TASK_UNUSED;

    /* Wait for timer IRQ to preempt us into another task */
    __asm__ volatile ("sti");
    for (;;)
        __asm__ volatile ("hlt");
}

void task_block(void *channel) {
    struct task *t = current;
    if (!t) return;
    t->state = TASK_BLOCKED;
    t->block_channel = channel;
}

void task_wake_one(void *channel) {
    struct task *head = current;
    if (!head) return;
    struct task *t = head;
    do {
        if (t->state == TASK_BLOCKED && t->block_channel == channel) {
            t->state = TASK_READY;
            t->block_channel = NULL;
            return;
        }
        t = t->next;
    } while (t != head);
}

void task_wake_all(void *channel) {
    struct task *head = current;
    if (!head) return;
    struct task *t = head;
    do {
        if (t->state == TASK_BLOCKED && t->block_channel == channel) {
            t->state = TASK_READY;
            t->block_channel = NULL;
        }
        t = t->next;
    } while (t != head);
}

struct task *task_find(uint64_t pid) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (task_pool[i].pid == pid && task_pool[i].state != TASK_UNUSED)
            return &task_pool[i];
    }
    return NULL;
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

            /* Destroy per-process address space (frees ELF + user stack pages) */
            if (t->cr3 && t->cr3 != vmm_get_boot_cr3()) {
                vmm_destroy_address_space(t->cr3);
                t->cr3 = 0;
            }

            /* Wake anyone waiting on this task */
            task_wake_all(t);

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
