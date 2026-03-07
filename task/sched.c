#include "task/task.h"
#include "task/signal.h"
#include "cpu/gdt.h"
#include "mm/vmm.h"
#include "lib/printf.h"

static void idle_entry(void) {
    for (;;)
        __asm__ volatile ("hlt");
}

void sched_init(void) {
    /* Create kernel task (pid 0) — represents current boot context */
    struct task *kernel_task = task_create("kernel", NULL);
    kernel_task->state = TASK_RUNNING;
    task_set_current(kernel_task);

    /* Create idle task — runs when nothing else is ready */
    task_create("idle", idle_entry);

    kprintf("[SCHED] Scheduler initialized\n");
}

/*
 * schedule_from_irq() — called from timer IRQ handler.
 * old_rsp points to the saved register frame on the interrupted task's stack.
 * Returns the RSP of the next task to run.
 */
uint64_t schedule_from_irq(uint64_t old_rsp) {
    struct task *cur = task_current();
    if (!cur || !cur->next)
        return old_rsp;

    int cur_alive = (cur->state == TASK_RUNNING || cur->state == TASK_READY ||
                     cur->state == TASK_BLOCKED);
    if (cur_alive) {
        cur->rsp = old_rsp;
        cur->ticks++;
    }

    /* Round-robin: find next READY task */
    struct task *next = cur->next;
    struct task *start = next;
    do {
        if (next->state == TASK_READY) break;
        next = next->next;
    } while (next != start);

    if (next->state != TASK_READY) {
        return old_rsp;
    }

    /* Context switch */
    if (cur_alive && cur->state == TASK_RUNNING)
        cur->state = TASK_READY;

    next->state = TASK_RUNNING;
    task_set_current(next);

    /* Update TSS.rsp0 for user-mode tasks */
    if (next->is_user) {
        tss_set_rsp0(next->kstack_top);
    }

    /* Switch address space if needed */
    uint64_t next_cr3 = next->cr3 ? next->cr3 : vmm_get_boot_cr3();
    uint64_t cur_cr3 = cur->cr3 ? cur->cr3 : vmm_get_boot_cr3();
    if (next_cr3 != cur_cr3) {
        vmm_switch_address_space(next_cr3);
    }

    /* Deliver pending signals before switching to next task */
    if (next->pending_signals && next->is_user) {
        signal_deliver(next);
        if (next->state == TASK_UNUSED)
            return old_rsp;
    }

    return next->rsp;
}
