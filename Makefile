# PlantOS Makefile — Two-stage build
# Stage 1: Build 64-bit kernel (kernel64.elf → kernel64.bin)
# Stage 2: Build 32-bit boot stub with embedded kernel (→ kernel.bin)

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

# 64-bit assembly sources (everything except boot.asm)
ASM64_SRCS = boot/long_mode.asm \
             cpu/gdt_flush.asm cpu/idt_flush.asm cpu/isr_stub.asm cpu/irq_stub.asm \
             cpu/syscall_stub.asm \
             task/switch.asm

# C sources
C_SRCS     = kernel/main.c kernel/panic.c \
             cpu/gdt.c cpu/idt.c cpu/isr.c cpu/irq.c cpu/syscall.c \
             drivers/vga.c drivers/serial.c drivers/pic.c drivers/pit.c drivers/keyboard.c \
             mm/pmm.c mm/vmm.c mm/heap.c \
             lib/string.c lib/printf.c lib/util.c \
             shell/shell.c shell/commands.c \
             task/task.c task/sched.c \
             fs/vfs.c fs/ramfs.c \
             user/demo.c

# Object files
ASM64_OBJS = $(patsubst %.asm,$(BUILDDIR)/%.o,$(ASM64_SRCS))
C_OBJS     = $(patsubst %.c,$(BUILDDIR)/%.o,$(C_SRCS))
OBJS64     = $(ASM64_OBJS) $(C_OBJS)

# Output files
KERNEL64_ELF = $(BUILDDIR)/kernel64.elf
KERNEL64_BIN = $(BUILDDIR)/kernel64.bin
BOOT_OBJ     = $(BUILDDIR)/boot/boot32.o
KERNEL       = $(BUILDDIR)/kernel.bin

QEMU = qemu-system-x86_64

.PHONY: all clean run debug dirs

all: $(KERNEL)

dirs:
	@mkdir -p $(BUILDDIR)/boot $(BUILDDIR)/kernel $(BUILDDIR)/cpu \
	          $(BUILDDIR)/drivers $(BUILDDIR)/mm $(BUILDDIR)/lib $(BUILDDIR)/shell \
	          $(BUILDDIR)/task $(BUILDDIR)/fs $(BUILDDIR)/user

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

# boot.asm depends on kernel64.bin (incbin)
$(BOOT_OBJ): boot/boot.asm $(KERNEL64_BIN) | dirs
	$(AS) -f elf32 $< -o $@

# Link final kernel (ELF32 — loadable by QEMU -kernel)
$(KERNEL): $(BOOT_OBJ) | dirs
	$(LD) $(LDFLAGS32) -o $@ $<

# --- Run ---

run: $(KERNEL)
	$(QEMU) -kernel $(KERNEL) -serial stdio -m 128M

debug: $(KERNEL)
	$(QEMU) -kernel $(KERNEL) -serial stdio -m 128M -s -S &
	@echo "Run: gdb $(KERNEL64_ELF) -ex 'target remote :1234'"

clean:
	rm -rf $(BUILDDIR)
