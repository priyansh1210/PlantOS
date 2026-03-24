# PlantOS Makefile — Two-stage build
# Stage 1: Build user ELF programs (with libc) → embed as binary objects
# Stage 2: Build 64-bit kernel (kernel64.elf → kernel64.bin)
# Stage 3: Build 32-bit boot stub with embedded kernel (→ kernel.bin)

CROSS   ?= x86_64-elf-
CC       = $(CROSS)gcc
LD       = $(CROSS)ld
OBJCOPY  = $(CROSS)objcopy
AS       = nasm

BUILDDIR = build

CFLAGS   = -ffreestanding -nostdlib -mno-red-zone -mcmodel=large \
           -mno-sse -mno-sse2 -mno-mmx -mno-avx \
           -Wall -Wextra -Wno-unused-parameter -O2 -g \
           -Iinclude -I.
LDFLAGS64 = -n -T linker64.ld -nostdlib
LDFLAGS32 = -n -T linker32.ld -nostdlib -m elf_i386

# User program flags (also freestanding, no SSE)
USER_CFLAGS = -ffreestanding -nostdlib -mno-red-zone -mcmodel=small \
              -mno-avx \
              -Wall -Wextra -Wno-unused-parameter -O2 -g \
              -Iinclude -I.
USER_LDFLAGS = -n -T user/linker_user.ld -nostdlib

# 64-bit assembly sources (everything except boot.asm)
ASM64_SRCS = boot/long_mode.asm \
             cpu/gdt_flush.asm cpu/idt_flush.asm cpu/isr_stub.asm cpu/irq_stub.asm \
             cpu/syscall_stub.asm \
             task/switch.asm

# C sources
C_SRCS     = kernel/main.c kernel/panic.c kernel/initrd.c kernel/env.c kernel/console.c \
             cpu/gdt.c cpu/idt.c cpu/isr.c cpu/irq.c cpu/syscall.c cpu/fpu.c cpu/apic.c \
             drivers/vga.c drivers/serial.c drivers/pic.c drivers/pit.c drivers/keyboard.c \
             drivers/pci.c drivers/ata.c drivers/fb.c drivers/mouse.c drivers/e1000.c \
             mm/pmm.c mm/vmm.c mm/heap.c mm/vma.c \
             lib/string.c lib/printf.c lib/util.c \
             shell/shell.c shell/commands.c shell/editor.c \
             net/net.c net/netbuf.c net/ethernet.c net/arp.c net/ipv4.c net/icmp.c net/udp.c net/tcp.c net/dns.c \
             gui/wm.c gui/terminal.c \
             task/task.c task/sched.c task/signal.c \
             fs/vfs.c fs/ramfs.c fs/elf_loader.c fs/pipe.c fs/fat.c fs/bcache.c \
             user/demo.c

# Object files
ASM64_OBJS = $(patsubst %.asm,$(BUILDDIR)/%.o,$(ASM64_SRCS))
C_OBJS     = $(patsubst %.c,$(BUILDDIR)/%.o,$(C_SRCS))

# User ELFs embedded via objcopy -I binary
USER_GEN_OBJS = $(BUILDDIR)/user/hello_elf.o $(BUILDDIR)/user/sigdemo_elf.o \
                $(BUILDDIR)/user/forkdemo_elf.o $(BUILDDIR)/user/mallocdemo_elf.o \
                $(BUILDDIR)/user/fpudemo_elf.o \
                $(BUILDDIR)/user/mathdemo_elf.o \
                $(BUILDDIR)/user/mmapdemo_elf.o \
                $(BUILDDIR)/user/cat_elf.o \
                $(BUILDDIR)/user/gfxdemo_elf.o \
                $(BUILDDIR)/user/filedemo_elf.o \
                $(BUILDDIR)/user/inputdemo_elf.o \
                $(BUILDDIR)/user/httpget_elf.o \
                $(BUILDDIR)/user/nndemo_elf.o \
                $(BUILDDIR)/user/httpserv_elf.o \
                $(BUILDDIR)/user/wc_elf.o \
                $(BUILDDIR)/user/grep_elf.o \
                $(BUILDDIR)/user/head_elf.o \
                $(BUILDDIR)/user/tail_elf.o \
                $(BUILDDIR)/user/sort_elf.o \
                $(BUILDDIR)/user/cp_elf.o \
                $(BUILDDIR)/user/mv_elf.o \
                $(BUILDDIR)/user/hexdump_elf.o \
                $(BUILDDIR)/user/echoserv_elf.o

# AP trampoline (flat binary, embedded via objcopy)
AP_TRAMP_BIN = $(BUILDDIR)/cpu/ap_tramp.bin
AP_TRAMP_OBJ = $(BUILDDIR)/cpu/ap_tramp_bin.o

OBJS64     = $(ASM64_OBJS) $(C_OBJS) $(USER_GEN_OBJS) $(AP_TRAMP_OBJ)

# Output files
KERNEL64_ELF = $(BUILDDIR)/kernel64.elf
KERNEL64_BIN = $(BUILDDIR)/kernel64.bin
BOOT_OBJ     = $(BUILDDIR)/boot/boot32.o
KERNEL       = $(BUILDDIR)/kernel.bin

# User libc objects (compiled with user flags)
USER_CRT0       = $(BUILDDIR)/user/crt0.o
USER_LIBC_OBJS  = $(BUILDDIR)/user/libc/ustring.o \
                  $(BUILDDIR)/user/libc/umalloc.o \
                  $(BUILDDIR)/user/libc/uprintf.o \
                  $(BUILDDIR)/user/libc/umath.o \
                  $(BUILDDIR)/user/libc/matrix.o \
                  $(BUILDDIR)/user/libc/nn.o

# User ELF intermediates
HELLO_ELF       = $(BUILDDIR)/user/hello.elf
SIGDEMO_ELF     = $(BUILDDIR)/user/sigdemo.elf
FORKDEMO_ELF    = $(BUILDDIR)/user/forkdemo.elf
MALLOCDEMO_ELF  = $(BUILDDIR)/user/mallocdemo.elf
FPUDEMO_ELF     = $(BUILDDIR)/user/fpudemo.elf
MATHDEMO_ELF    = $(BUILDDIR)/user/mathdemo.elf
MMAPDEMO_ELF    = $(BUILDDIR)/user/mmapdemo.elf
CAT_ELF         = $(BUILDDIR)/user/cat.elf
GFXDEMO_ELF     = $(BUILDDIR)/user/gfxdemo.elf
FILEDEMO_ELF    = $(BUILDDIR)/user/filedemo.elf
INPUTDEMO_ELF   = $(BUILDDIR)/user/inputdemo.elf
HTTPGET_ELF     = $(BUILDDIR)/user/httpget.elf
NNDEMO_ELF      = $(BUILDDIR)/user/nndemo.elf
HTTPSERV_ELF    = $(BUILDDIR)/user/httpserv.elf
WC_ELF          = $(BUILDDIR)/user/wc.elf
GREP_ELF        = $(BUILDDIR)/user/grep.elf
HEAD_ELF        = $(BUILDDIR)/user/head.elf
TAIL_ELF        = $(BUILDDIR)/user/tail.elf
SORT_ELF        = $(BUILDDIR)/user/sort.elf
CP_ELF          = $(BUILDDIR)/user/cp.elf
MV_ELF          = $(BUILDDIR)/user/mv.elf
HEXDUMP_ELF     = $(BUILDDIR)/user/hexdump.elf
ECHOSERV_ELF    = $(BUILDDIR)/user/echoserv.elf

QEMU = /d/msys64/mingw64/bin/qemu-system-x86_64

.PHONY: all clean run debug dirs

all: $(KERNEL)

dirs:
	@mkdir -p $(BUILDDIR)/boot $(BUILDDIR)/kernel $(BUILDDIR)/cpu \
	          $(BUILDDIR)/drivers $(BUILDDIR)/mm $(BUILDDIR)/lib $(BUILDDIR)/shell \
	          $(BUILDDIR)/task $(BUILDDIR)/fs $(BUILDDIR)/net $(BUILDDIR)/gui $(BUILDDIR)/user $(BUILDDIR)/user/libc

# --- User libc ---

$(BUILDDIR)/user/libc/%.o: user/libc/%.c | dirs
	$(CC) $(USER_CFLAGS) -c $< -o $@

# --- User ELF programs (all link with crt0 + libc) ---

$(USER_CRT0): user/crt0.asm | dirs
	$(AS) -f elf64 $< -o $@

# Generic rule: compile user source to _user.o
$(BUILDDIR)/user/%_user.o: user/%.c | dirs
	$(CC) $(USER_CFLAGS) -c $< -o $@

# hello
$(HELLO_ELF): $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/hello_user.o | dirs
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/hello_user.o

$(BUILDDIR)/user/hello_elf.o: $(HELLO_ELF) | dirs
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	  --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	  $< $@

# sigdemo
$(SIGDEMO_ELF): $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/sigdemo_user.o | dirs
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/sigdemo_user.o

$(BUILDDIR)/user/sigdemo_elf.o: $(SIGDEMO_ELF) | dirs
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	  --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	  $< $@

# forkdemo
$(FORKDEMO_ELF): $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/forkdemo_user.o | dirs
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/forkdemo_user.o

$(BUILDDIR)/user/forkdemo_elf.o: $(FORKDEMO_ELF) | dirs
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	  --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	  $< $@

# mallocdemo
$(MALLOCDEMO_ELF): $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/mallocdemo_user.o | dirs
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/mallocdemo_user.o

$(BUILDDIR)/user/mallocdemo_elf.o: $(MALLOCDEMO_ELF) | dirs
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	  --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	  $< $@

# fpudemo
$(FPUDEMO_ELF): $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/fpudemo_user.o | dirs
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/fpudemo_user.o

$(BUILDDIR)/user/fpudemo_elf.o: $(FPUDEMO_ELF) | dirs
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	  --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	  $< $@

# mathdemo
$(MATHDEMO_ELF): $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/mathdemo_user.o | dirs
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/mathdemo_user.o

$(BUILDDIR)/user/mathdemo_elf.o: $(MATHDEMO_ELF) | dirs
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	  --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	  $< $@

# mmapdemo
$(MMAPDEMO_ELF): $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/mmapdemo_user.o | dirs
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/mmapdemo_user.o

$(BUILDDIR)/user/mmapdemo_elf.o: $(MMAPDEMO_ELF) | dirs
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	  --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	  $< $@

# cat
$(CAT_ELF): $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/cat_user.o | dirs
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/cat_user.o

$(BUILDDIR)/user/cat_elf.o: $(CAT_ELF) | dirs
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	  --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	  $< $@

# gfxdemo
$(GFXDEMO_ELF): $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/gfxdemo_user.o | dirs
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/gfxdemo_user.o

$(BUILDDIR)/user/gfxdemo_elf.o: $(GFXDEMO_ELF) | dirs
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	  --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	  $< $@

# filedemo
$(FILEDEMO_ELF): $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/filedemo_user.o | dirs
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/filedemo_user.o

$(BUILDDIR)/user/filedemo_elf.o: $(FILEDEMO_ELF) | dirs
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	  --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	  $< $@

# inputdemo
$(INPUTDEMO_ELF): $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/inputdemo_user.o | dirs
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/inputdemo_user.o

$(BUILDDIR)/user/inputdemo_elf.o: $(INPUTDEMO_ELF) | dirs
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	  --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	  $< $@

# nndemo
$(NNDEMO_ELF): $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/nndemo_user.o | dirs
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/nndemo_user.o

$(BUILDDIR)/user/nndemo_elf.o: $(NNDEMO_ELF) | dirs
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	  --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	  $< $@

# httpget
$(HTTPGET_ELF): $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/httpget_user.o | dirs
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/httpget_user.o

$(BUILDDIR)/user/httpget_elf.o: $(HTTPGET_ELF) | dirs
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	  --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	  $< $@

# httpserv
$(HTTPSERV_ELF): $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/httpserv_user.o | dirs
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/httpserv_user.o

$(BUILDDIR)/user/httpserv_elf.o: $(HTTPSERV_ELF) | dirs
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	  --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	  $< $@

# wc
$(WC_ELF): $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/wc_user.o | dirs
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/wc_user.o

$(BUILDDIR)/user/wc_elf.o: $(WC_ELF) | dirs
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	  --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	  $< $@

# grep
$(GREP_ELF): $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/grep_user.o | dirs
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/grep_user.o

$(BUILDDIR)/user/grep_elf.o: $(GREP_ELF) | dirs
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	  --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	  $< $@

# head
$(HEAD_ELF): $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/head_user.o | dirs
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/head_user.o

$(BUILDDIR)/user/head_elf.o: $(HEAD_ELF) | dirs
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	  --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	  $< $@

# tail
$(TAIL_ELF): $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/tail_user.o | dirs
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/tail_user.o

$(BUILDDIR)/user/tail_elf.o: $(TAIL_ELF) | dirs
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	  --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	  $< $@

# sort
$(SORT_ELF): $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/sort_user.o | dirs
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/sort_user.o

$(BUILDDIR)/user/sort_elf.o: $(SORT_ELF) | dirs
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	  --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	  $< $@

# cp
$(CP_ELF): $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/cp_user.o | dirs
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/cp_user.o

$(BUILDDIR)/user/cp_elf.o: $(CP_ELF) | dirs
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	  --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	  $< $@

# mv
$(MV_ELF): $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/mv_user.o | dirs
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/mv_user.o

$(BUILDDIR)/user/mv_elf.o: $(MV_ELF) | dirs
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	  --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	  $< $@

# hexdump
$(HEXDUMP_ELF): $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/hexdump_user.o | dirs
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/hexdump_user.o

$(BUILDDIR)/user/hexdump_elf.o: $(HEXDUMP_ELF) | dirs
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	  --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	  $< $@

# echoserv
$(ECHOSERV_ELF): $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/echoserv_user.o | dirs
	$(LD) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $(USER_LIBC_OBJS) $(BUILDDIR)/user/echoserv_user.o

$(BUILDDIR)/user/echoserv_elf.o: $(ECHOSERV_ELF) | dirs
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	  --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	  $< $@

# --- AP trampoline (flat binary → embedded object) ---

$(AP_TRAMP_BIN): cpu/ap_tramp.asm | dirs
	$(AS) -f bin $< -o $@

$(AP_TRAMP_OBJ): $(AP_TRAMP_BIN) | dirs
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	  --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	  $< $@

# --- Stage 1: 64-bit kernel ---

# Assemble 64-bit ASM
$(BUILDDIR)/%.o: %.asm | dirs
	$(AS) -f elf64 $< -o $@

# Compile C
$(BUILDDIR)/%.o: %.c | dirs
	$(CC) $(CFLAGS) -c $< -o $@

# Link 64-bit kernel
$(KERNEL64_ELF): $(OBJS64) | dirs
	$(LD) $(LDFLAGS64) -o $@ $(OBJS64)

# Convert to flat binary
$(KERNEL64_BIN): $(KERNEL64_ELF)
	$(OBJCOPY) -O binary $< $@

# --- Stage 2: 32-bit boot stub ---

$(BOOT_OBJ): boot/boot.asm $(KERNEL64_BIN) | dirs
	$(AS) -f elf32 $< -o $@

$(KERNEL): $(BOOT_OBJ) | dirs
	$(LD) $(LDFLAGS32) -o $@ $<

# --- Run ---

run: $(KERNEL)
	$(QEMU) -kernel $(KERNEL) -serial stdio -m 128M -smp 4 \
	  -drive file=disk.img,format=raw,if=ide,index=0 \
	  -netdev user,id=net0,hostfwd=tcp::8080-:80 -device e1000,netdev=net0

debug: $(KERNEL)
	$(QEMU) -kernel $(KERNEL) -serial stdio -m 128M \
	  -drive file=disk.img,format=raw,if=ide,index=0 -s -S &
	@echo "Run: gdb $(KERNEL64_ELF) -ex 'target remote :1234'"

clean:
	rm -rf $(BUILDDIR)
