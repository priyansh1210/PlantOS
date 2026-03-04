; syscall_stub.asm — INT 0x80 entry point
; Same register save/restore layout as IRQ stubs
bits 64

extern syscall_handler

global syscall_stub

syscall_stub:
    push 0              ; Dummy error code
    push 0x80           ; Interrupt number

    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp        ; Pass pointer to register frame
    call syscall_handler

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax             ; Return value already placed in frame by handler

    add rsp, 16         ; Skip int_no + err_code
    iretq
