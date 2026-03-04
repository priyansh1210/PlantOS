; boot.asm — 32-bit multiboot stub: sets up paging, enables long mode, jumps to 64-bit kernel
bits 32

; Multiboot 1 constants
MULTIBOOT_MAGIC     equ 0x1BADB002
MULTIBOOT_FLAGS     equ 0x00000003   ; Align modules + provide memory map
MULTIBOOT_CHECKSUM  equ -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

; Address where 64-bit kernel is loaded
KERNEL64_ENTRY      equ 0x200000

section .multiboot
align 4
    dd MULTIBOOT_MAGIC
    dd MULTIBOOT_FLAGS
    dd MULTIBOOT_CHECKSUM

section .bss
align 4096
; Page tables — identity map first 4GB with 2MB huge pages
pml4_table:
    resb 4096
pdpt_table:
    resb 4096
pd_table:
    resb 4096
pd_table2:
    resb 4096
pd_table3:
    resb 4096
pd_table4:
    resb 4096

align 16
stack_bottom:
    resb 16384          ; 16KB stack for boot + early kernel use
stack_top:

section .text
global _start

_start:
    ; Save multiboot info pointer
    mov edi, eax        ; multiboot magic
    mov esi, ebx        ; multiboot info structure pointer

    ; Set up boot stack (32-bit)
    mov esp, stack_top

    ; Check CPUID availability
    call .check_cpuid
    ; Check long mode support
    call .check_long_mode
    ; Set up paging
    call .setup_paging
    ; Enable PAE
    call .enable_pae
    ; Enable long mode
    call .enable_long_mode
    ; Enable paging
    call .enable_paging

    ; Load 64-bit GDT and far jump to 64-bit kernel at 0x200000
    lgdt [gdt64.pointer]
    jmp dword gdt64.code_segment:KERNEL64_ENTRY

.check_cpuid:
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 1 << 21
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    cmp eax, ecx
    je .no_cpuid
    ret
.no_cpuid:
    mov dword [0xB8000], 0x4F434F4E  ; "NC" — No CPUID
    jmp .halt

.check_long_mode:
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no_long_mode
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz .no_long_mode
    ret
.no_long_mode:
    mov dword [0xB8000], 0x4F4C4F4E  ; "NL" — No Long mode
    jmp .halt

.setup_paging:
    ; PML4[0] -> PDPT
    mov eax, pdpt_table
    or eax, 0x07           ; Present + Writable + User
    mov [pml4_table], eax

    ; PDPT[0-3] -> PD tables
    mov eax, pd_table
    or eax, 0x07
    mov [pdpt_table], eax

    mov eax, pd_table2
    or eax, 0x07
    mov [pdpt_table + 8], eax

    mov eax, pd_table3
    or eax, 0x07
    mov [pdpt_table + 16], eax

    mov eax, pd_table4
    or eax, 0x07
    mov [pdpt_table + 24], eax

    ; Map 4GB using 2MB huge pages
    mov ecx, 0

.map_page:
    mov eax, ecx
    shl eax, 21            ; eax = ecx * 2MB
    or eax, 0x87           ; Present + Writable + User + Huge

    mov edx, ecx
    shr edx, 9             ; PD index (0-3)
    mov ebx, ecx
    and ebx, 0x1FF         ; Entry index (0-511)

    push eax
    mov eax, edx
    shl eax, 12
    add eax, pd_table
    lea eax, [eax + ebx*8]
    mov edx, eax
    pop eax

    mov [edx], eax
    mov dword [edx + 4], 0

    inc ecx
    cmp ecx, 2048          ; 2048 * 2MB = 4GB
    jl .map_page
    ret

.enable_pae:
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax
    ret

.enable_long_mode:
    mov eax, pml4_table
    mov cr3, eax
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr
    ret

.enable_paging:
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax
    ret

.halt:
    cli
    hlt
    jmp .halt

; Embed the 64-bit kernel binary
section .kernel64
incbin "build/kernel64.bin"

; Temporary 64-bit GDT (just for the mode switch)
section .rodata
align 16
gdt64:
    dq 0                            ; Null descriptor
.code_segment: equ $ - gdt64
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53)   ; Code: exec, present, 64-bit
.data_segment: equ $ - gdt64
    dq (1<<44) | (1<<47) | (1<<41)              ; Data: present, writable
.pointer:
    dw $ - gdt64 - 1       ; Limit (16-bit)
    dd gdt64                ; Base (32-bit, sufficient for < 4GB)
    dd 0                    ; Padding for 64-bit base
