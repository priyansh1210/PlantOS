; crt0.asm — C runtime startup for user-mode ELF programs
; Receives argc in rdi, argv in rsi (set up by kernel).
; Calls main(argc, argv), then issues SYS_EXIT with the return value.

[BITS 64]

section .text._start
global _start
extern main

_start:
    ; argc is already in rdi, argv in rsi (System V ABI)
    call main

    ; SYS_EXIT(return_value)
    mov rdi, rax        ; exit code = main()'s return value
    mov rax, 1          ; SYS_EXIT = 1
    int 0x80

    ; Should never reach here
.hang:
    hlt
    jmp .hang
