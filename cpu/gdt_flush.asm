; gdt_flush.asm — Load GDT, reload segment registers, load TSS
bits 64

section .text
global gdt_flush
global tss_load

gdt_flush:
    lgdt [rdi]          ; Load GDT pointer (passed in rdi — System V ABI)
    ; Reload CS via far return
    mov ax, 0x10        ; Kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ; Far jump to reload CS
    push 0x08           ; Kernel code segment
    lea rax, [rel .flush]
    push rax
    retfq
.flush:
    ret

tss_load:
    mov ax, di          ; TSS selector passed in rdi (lower 16 bits)
    ltr ax
    ret
