; switch.asm — Voluntary context switch
; void task_switch_to(uint64_t *old_rsp, uint64_t new_rsp)
;   rdi = pointer to current task's saved RSP field
;   rsi = new task's RSP value
;
; Saves callee-saved registers on old stack, swaps RSP, restores from new stack.

bits 64

global task_switch_to

task_switch_to:
    ; Save callee-saved registers on current stack
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    ; Save current RSP to *old_rsp
    mov [rdi], rsp

    ; Load new RSP
    mov rsp, rsi

    ; Restore callee-saved registers from new stack
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    ret
