; long_mode.asm — 64-bit entry point (must be first in .text for flat binary)
bits 64

extern kernel_main
extern _bss_start
extern _bss_end

; Use a special section to ensure this is placed first in the binary
section .text.entry
global long_mode_entry

long_mode_entry:
    ; Reload data segment registers
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Set up 64-bit stack (reuse boot stack at known address below 2MB)
    ; stack_top is defined in boot.asm BSS, located just below 0x200000
    ; We use a fixed address since we can't reference 32-bit symbols
    mov rsp, 0x1F0000      ; Safe stack address in identity-mapped region

    ; Zero BSS section
    lea rdi, [rel _bss_start]
    lea rcx, [rel _bss_end]
    sub rcx, rdi
    shr rcx, 3
    xor rax, rax
    rep stosq

    ; Pass multiboot info to kernel_main
    ; ESI still holds multiboot info pointer from 32-bit boot code
    ; In 64-bit mode, ESI is zero-extended to RSI automatically
    ; Move to RDI (first argument in System V AMD64 ABI)
    mov edi, esi

    call kernel_main

    ; If kernel_main returns, halt
.halt:
    cli
    hlt
    jmp .halt
