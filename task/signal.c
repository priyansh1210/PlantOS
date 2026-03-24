#include "task/signal.h"
#include <plantos/signal.h>
#include "cpu/idt.h"
#include "lib/printf.h"
#include "fs/elf_loader.h"

void signal_init(void) {
    kprintf("[SIGNAL] Signal subsystem initialized\n");
}

int signal_send(uint64_t pid, int signum) {
    if (signum < 1 || signum >= MAX_SIGNALS)
        return -1;

    struct task *t = task_find(pid);
    if (!t)
        return -1;

    /* Only deliver signals to user tasks (kernel tasks ignore signals) */
    if (!t->is_user)
        return -1;

    /* SIGCONT: always resume stopped task, clear pending stop signals */
    if (signum == SIGCONT) {
        t->pending_signals &= ~((1u << SIGTSTP) | (1u << SIGSTOP));
        if (t->state == TASK_STOPPED) {
            task_continue(t);
        }
        return 0;
    }

    /* SIGTSTP / SIGSTOP: stop the task immediately */
    if (signum == SIGTSTP || signum == SIGSTOP) {
        /* SIGSTOP cannot be caught; SIGTSTP can be caught */
        if (signum == SIGSTOP || !t->signal_handlers[SIGTSTP]) {
            task_stop(t);
            return 0;
        }
        /* SIGTSTP with user handler — deliver via normal path */
    }

    t->pending_signals |= (1u << signum);

    /* If target is blocked, wake it so it can receive the signal */
    if (t->state == TASK_BLOCKED) {
        t->state = TASK_READY;
        t->block_channel = NULL;
    }

    return 0;
}

int signal_register(int signum, uint64_t handler_addr) {
    if (signum < 1 || signum >= MAX_SIGNALS)
        return -1;
    if (signum == SIGKILL || signum == SIGSTOP)
        return -1; /* Cannot override SIGKILL or SIGSTOP */

    struct task *cur = task_current();
    if (!cur)
        return -1;

    cur->signal_handlers[signum] = handler_addr;
    return 0;
}

/*
 * Deliver pending signals to task t before it resumes.
 * Manipulates the saved iretq frame on the kernel stack.
 *
 * Frame layout at t->rsp:
 *   [0]  r15  [1]  r14  [2]  r13  [3]  r12
 *   [4]  r11  [5]  r10  [6]  r9   [7]  r8
 *   [8]  rbp  [9]  rdi  [10] rsi  [11] rdx
 *   [12] rcx  [13] rbx  [14] rax
 *   [15] int_no  [16] err_code
 *   [17] rip  [18] cs  [19] rflags  [20] rsp  [21] ss
 */
void signal_deliver(struct task *t) {
    if (!t->pending_signals || !t->is_user)
        return;

    uint64_t *frame = (uint64_t *)t->rsp;

    for (int sig = 1; sig < MAX_SIGNALS; sig++) {
        if (!(t->pending_signals & (1u << sig)))
            continue;

        /* Clear the pending bit */
        t->pending_signals &= ~(1u << sig);

        if (sig == SIGKILL) {
            /* SIGKILL: unconditional termination */
            kprintf("[SIGNAL] SIGKILL → pid %llu\n", t->pid);
            /* Free ELF pages */
            if (t->elf_num_pages > 0) {
                elf_unload(t->elf_load_base, t->elf_num_pages);
                t->elf_load_base = 0;
                t->elf_num_pages = 0;
            }
            /* Unlink from run ring — use task_kill-like cleanup */
            if (t->stack_base) {
                extern void kfree(void *);
                kfree(t->stack_base);
                t->stack_base = NULL;
            }
            if (t->kstack_base) {
                extern void kfree(void *);
                kfree(t->kstack_base);
                t->kstack_base = NULL;
            }
            /* Unlink */
            struct task *prev = t;
            while (prev->next != t)
                prev = prev->next;
            if (prev != t)
                prev->next = t->next;
            t->state = TASK_UNUSED;
            return;
        }

        uint64_t handler = t->signal_handlers[sig];
        if (!handler) {
            /* Default action: terminate */
            kprintf("[SIGNAL] Signal %d (default=kill) → pid %llu\n", sig, t->pid);
            if (t->elf_num_pages > 0) {
                elf_unload(t->elf_load_base, t->elf_num_pages);
                t->elf_load_base = 0;
                t->elf_num_pages = 0;
            }
            if (t->stack_base) {
                extern void kfree(void *);
                kfree(t->stack_base);
                t->stack_base = NULL;
            }
            if (t->kstack_base) {
                extern void kfree(void *);
                kfree(t->kstack_base);
                t->kstack_base = NULL;
            }
            struct task *prev = t;
            while (prev->next != t)
                prev = prev->next;
            if (prev != t)
                prev->next = t->next;
            t->state = TASK_UNUSED;
            return;
        }

        /* Deliver signal via iretq frame manipulation */
        uint64_t saved_rip    = frame[17];
        uint64_t saved_rflags = frame[19];
        uint64_t user_rsp     = frame[20];

        /* Push trampoline data onto user stack:
         *   [user_rsp - 24] saved_rip
         *   [user_rsp - 16] saved_rflags
         *   [user_rsp - 8]  signum
         */
        user_rsp -= 8;
        *(uint64_t *)user_rsp = (uint64_t)sig;
        user_rsp -= 8;
        *(uint64_t *)user_rsp = saved_rflags;
        user_rsp -= 8;
        *(uint64_t *)user_rsp = saved_rip;

        /* Update iretq frame */
        frame[17] = handler;         /* RIP = signal handler */
        frame[20] = user_rsp;        /* RSP = modified user stack */
        frame[9]  = (uint64_t)sig;   /* RDI = signum (first argument) */

        /* Only deliver one signal per scheduling tick */
        return;
    }
}

/*
 * SYS_SIGRETURN: Restore pre-signal state from user stack trampoline.
 * Called from syscall handler with regs pointing to the INT 0x80 frame.
 */
void signal_return(struct registers *regs) {
    uint64_t user_rsp = regs->rsp;

    /* Pop trampoline: saved_rip, saved_rflags, signum */
    uint64_t saved_rip    = *(uint64_t *)user_rsp;
    user_rsp += 8;
    uint64_t saved_rflags = *(uint64_t *)user_rsp;
    user_rsp += 8;
    /* uint64_t signum = *(uint64_t *)user_rsp; — not needed */
    user_rsp += 8;

    /* Restore original execution point */
    regs->rip    = saved_rip;
    regs->rflags = saved_rflags;
    regs->rsp    = user_rsp;
}
