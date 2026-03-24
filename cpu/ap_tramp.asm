; ap_tramp.asm — AP (Application Processor) boot trampoline
; Assembled as flat binary, copied to physical 0x8000 at runtime.
; BSP patches the data area (last 24 bytes) before sending SIPI.

bits 16
org 0x8000

ap_tramp_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; Load 32-bit GDT
    lgdt [0x8000 + (ap_gdt32_ptr - ap_tramp_start)]

    ; Enable protected mode
    mov eax, cr0
    or al, 1
    mov cr0, eax

    ; Far jump to 32-bit code
    jmp dword 0x08:(0x8000 + (ap_pm32 - ap_tramp_start))

bits 32
ap_pm32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; Enable PAE (CR4.PAE = bit 5)
    mov eax, cr4
    or eax, (1 << 5)
    mov cr4, eax

    ; Load PML4 table address (BSP patches this)
    mov eax, [0x8000 + (ap_pml4_addr - ap_tramp_start)]
    mov cr3, eax

    ; Enable long mode (EFER.LME = bit 8)
    mov ecx, 0xC0000080
    rdmsr
    or eax, (1 << 8)
    wrmsr

    ; Enable paging (CR0.PG = bit 31)
    mov eax, cr0
    or eax, (1 << 31)
    mov cr0, eax

    ; Load 64-bit GDT and far jump to long mode
    lgdt [0x8000 + (ap_gdt64_ptr - ap_tramp_start)]
    jmp dword 0x08:(0x8000 + (ap_lm64 - ap_tramp_start))

bits 64
ap_lm64:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Load stack pointer (BSP patches this)
    mov rsp, [0x8000 + (ap_stack_ptr - ap_tramp_start)]

    ; Get APIC ID via CPUID leaf 1 (EBX[31:24])
    mov eax, 1
    cpuid
    shr ebx, 24
    and ebx, 0xFF
    mov edi, ebx   ; first argument = APIC ID

    ; Call C entry function (BSP patches this)
    mov rax, [0x8000 + (ap_entry_fn - ap_tramp_start)]
    call rax

    ; Should not return
    cli
.halt:
    hlt
    jmp .halt

; ---- 32-bit GDT (for PM transition) ----
align 8
ap_gdt32:
    dq 0                              ; Null
    ; Code32: base=0, limit=4G, 32-bit, DPL=0, execute/read
    dw 0xFFFF, 0x0000
    db 0x00, 0x9A, 0xCF, 0x00
    ; Data32: base=0, limit=4G, 32-bit, DPL=0, read/write
    dw 0xFFFF, 0x0000
    db 0x00, 0x92, 0xCF, 0x00
ap_gdt32_ptr:
    dw (ap_gdt32_ptr - ap_gdt32 - 1)
    dd 0x8000 + (ap_gdt32 - ap_tramp_start)

; ---- 64-bit GDT (for long mode) ----
align 8
ap_gdt64:
    dq 0                              ; Null
    ; Code64: L=1, D=0, DPL=0
    dw 0x0000, 0x0000
    db 0x00, 0x9A, 0xA0, 0x00
    ; Data64: DPL=0
    dw 0x0000, 0x0000
    db 0x00, 0x92, 0xC0, 0x00
ap_gdt64_ptr:
    dw (ap_gdt64_ptr - ap_gdt64 - 1)
    dq 0x8000 + (ap_gdt64 - ap_tramp_start)

; ---- Patchable data area (MUST be last 24 bytes of binary) ----
; BSP writes: PML4 address, stack pointer, C entry function
align 8
ap_pml4_addr:  dd 0         ; 32-bit physical address of PML4 (offset: end-24)
               dd 0         ; padding
ap_stack_ptr:  dq 0         ; 64-bit stack pointer (offset: end-16)
ap_entry_fn:   dq 0         ; 64-bit C entry function (offset: end-8)

ap_tramp_end:
