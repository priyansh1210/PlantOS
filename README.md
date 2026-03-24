# PlantOS

A 64-bit operating system built from scratch in C and x86 Assembly, targeting QEMU. PlantOS implements everything from bootloader to a neural network engine -- no external libraries, no standard C library, every byte written by hand.

## Features

### Core OS
- **x86_64 long mode** with multiboot1 boot, GDT/IDT, paging (2MB huge pages), and TSS
- **Preemptive multitasking** with round-robin scheduler and per-process address spaces
- **User mode (Ring 3)** with 29 syscalls via `INT 0x80` (write, read, fork, exec, mmap, socket, ...)
- **Virtual memory** with per-process page tables, demand paging, copy-on-write fork, VMAs, mmap/munmap
- **Unix signals** (SIGINT, SIGKILL, SIGTERM, SIGCHLD, etc.) and pipe IPC

### Filesystem & Storage
- **VFS layer** routing between RAM filesystem and FAT32
- **ATA PIO disk driver** with 64-sector LRU buffer cache
- **FAT32** read/write support for persistent storage
- **ELF64 loader** for executing user-space programs

### Networking
- **E1000 NIC driver** (Intel 82540EM) with MMIO, DMA ring buffers, and IRQ handling
- **Full TCP/IP stack**: Ethernet, ARP, IPv4, ICMP (ping), UDP, TCP (full state machine with retransmission)
- **DNS client** with caching (queries QEMU's built-in DNS at 10.0.2.3)
- **HTTP client & server** -- make HTTP requests and serve web pages from the OS
- **Sockets API** with connect/send/recv/listen/accept

### Graphics & GUI
- **Bochs VBE framebuffer** (640x480x32bpp) with 8x16 bitmap font
- **Window manager** with draggable windows, z-order, close buttons, double-buffered compositing
- **Terminal emulator** window with keyboard input and command execution
- **PS/2 mouse driver** with cursor rendering

### Shell & Utilities
Interactive shell with 40+ built-in commands:
```
help, clear, echo, ls, cat, touch, mkdir, rm, write, cd, pwd,
ps, kill, exec, spawn, pipe, edit, meminfo, heap, ticks, jobs,
mount, umount, sync, lspci, diskinfo, netinfo, ping, http,
resolve, tcptest, desktop, env, setenv, reboot, about, ...
```
User-space utilities: `cat`, `wc`, `grep`, `head`, `tail`, `sort`, `cp`, `mv`, `hexdump`, `httpget`, `httpserv`

### Neural Network Engine

PlantOS includes a **complete neural network library** that runs entirely in user space on bare metal -- no OS libraries, no floating-point emulation, pure x87/SSE hardware math.

#### Architecture

The NN engine is split into two layers:

**Matrix Library** (`user/libc/matrix.c/h`)
- Heap-allocated row-major matrices with dynamic sizing
- Operations: multiply, transpose-multiply (`A^T*B`, `A*B^T`), Hadamard product, bias addition, column summation
- Xavier random initialization via xorshift PRNG

**Neural Network Library** (`user/libc/nn.c/h`)
- Configurable dense (fully-connected) layers, up to 16 layers deep
- **5 activation functions**: sigmoid, tanh, ReLU, softmax, linear
- **2 loss functions**: MSE and binary/categorical cross-entropy
- **SGD optimizer with momentum** (configurable learning rate and momentum coefficient)
- **Xavier weight initialization** for stable training
- **Batched training** -- forward pass, loss computation, backpropagation, and weight update in a single `nn_train_step()` call
- Numerically stable softmax (max-subtraction trick) and clamped log for cross-entropy

#### How It Works

The training loop follows standard deep learning practice:

1. **Forward pass**: For each layer, compute `z = W * input + bias`, then apply activation function to get output `a`
2. **Loss computation**: MSE or cross-entropy between predictions and targets
3. **Backpropagation**: Compute gradients `dW` and `db` for each layer by propagating error backwards through the chain rule. Uses simplified gradients for common pairs (sigmoid+cross-entropy, softmax+cross-entropy)
4. **Weight update**: SGD with momentum: `v = momentum * v + lr * gradient`, then `W -= v`

#### Model Persistence

Trained models can be saved to and loaded from FAT32 disk using a custom binary format (`PLNN`):
```
Header: "PLNN" magic | num_layers | loss_fn | learning_rate | momentum
Per layer: in_size | out_size | activation | weight data (float64) | bias data (float64)
```

#### Demo Results

The `nndemo` program demonstrates two tasks:

**XOR Learning** (2 -> 8 -> 1 network)
- Architecture: 2 inputs, 8 hidden neurons (tanh), 1 output (sigmoid)
- Trains with binary cross-entropy + SGD (momentum=0.9)
- Achieves 4/4 accuracy after 5000 epochs
- Model saved to `/disk/xor.nn`, loaded back, predictions verified

**Circle Classification** (2 -> 16 -> 8 -> 1 network)
- Classifies 2D points as inside/outside a circle (radius 0.8)
- 3-layer network with ReLU hidden layers and sigmoid output
- Trains on 32 samples for 3000 epochs
- Learns the nonlinear decision boundary successfully

#### What Makes It Interesting

This isn't a neural network running on Linux or using PyTorch. Every piece of the stack is custom:
- The **math functions** (exp, log, sqrt, tanh, etc.) are implemented using x87 FPU instructions directly
- The **memory allocator** (malloc/free) is a custom heap implementation with sbrk syscall
- The **floating-point hardware** (FPU/SSE) is initialized and managed per-process by the OS kernel
- The **file I/O** for model save/load goes through custom syscalls to a custom FAT32 driver
- The **printf** that displays training progress is a from-scratch implementation

The entire neural network -- from matrix multiplication to gradient descent to disk persistence -- runs on bare metal with zero external dependencies.

## Building

### Prerequisites
- **NASM** assembler
- **x86_64-elf-gcc** cross-compiler (GCC)
- **QEMU** (qemu-system-x86_64)
- **mtools** (mcopy, mmd) for FAT32 disk image manipulation

### Build & Run
```bash
make          # Build the kernel
make run      # Launch in QEMU with networking and disk
make clean    # Clean build artifacts
```

QEMU is launched with:
- 128MB RAM, 4 CPU cores
- IDE disk (disk.img, 32MB FAT32)
- E1000 NIC with user-mode networking (port 8080 forwarded to guest port 80)
- Serial output to stdio

## Project Structure

```
PlantOS/
├── boot/           # Multiboot1 boot stub (32-bit), long mode transition
├── kernel/         # Main kernel, panic handler, initrd, console, env vars
├── cpu/            # GDT, IDT, ISR, IRQ, syscalls, FPU, APIC, spinlocks
├── drivers/        # VGA, serial, PIC, PIT, keyboard, mouse, PCI, ATA, E1000, framebuffer
├── mm/             # Physical memory manager, virtual memory, heap, VMAs
├── lib/            # String, printf, utilities (kernel-side)
├── fs/             # VFS, ramfs, FAT32, ELF loader, pipes, buffer cache
├── task/           # Task management, scheduler, context switch, signals
├── shell/          # Interactive shell, commands, text editor
├── gui/            # Window manager, terminal emulator
├── net/            # Ethernet, ARP, IPv4, ICMP, UDP, TCP, DNS
├── user/           # User-space programs and libc
│   └── libc/       # malloc, printf, string, math, matrix, neural network
└── include/        # Shared headers (types, multiboot, signals, ELF)
```

## Milestones

| # | Milestone | Description |
|---|-----------|-------------|
| 1 | Boot & Hardware | Serial, GDT/IDT, PIC/IRQ, PIT timer, keyboard, PMM/VMM/heap |
| 2 | Multitasking | Preemptive round-robin scheduler |
| 3 | User Mode | TSS, INT 0x80 syscalls, Ring 3 tasks |
| 4 | ELF Loader | Load and execute ELF64 user programs |
| 5 | Signals & Pipes | Unix signals, blocking pipe IPC |
| 6 | Process Isolation | Per-process address spaces, fork(), waitpid |
| 7 | User Libc | malloc, printf, string library, sbrk, page fault handler |
| 8 | Disk & FAT32 | PCI, ATA PIO driver, FAT32 filesystem, VFS routing |
| 9 | FPU/SSE | Hardware FP support, per-task state save/restore |
| 10 | Math Library | 40+ math functions (x87 hardware-accelerated) |
| 11 | Virtual Memory | VMAs, mmap/munmap, demand paging |
| 12 | Buffer Cache | 64-sector LRU cache for disk I/O |
| 13 | Process Args | argc/argv, exit codes, zombie reaping |
| 14 | POSIX File API | open, fstat, mkdir, user-space cat |
| 15 | Framebuffer | Bochs VBE 640x480x32, 8x16 font, gfxdemo |
| 16 | Mouse | PS/2 mouse driver, graphical cursor |
| 17 | Window Manager | Draggable windows, z-order, compositing |
| 18 | Terminal Emulator | GUI terminal with keyboard input |
| 19 | Environment | Env vars, working directory, PATH lookup |
| 20 | File I/O | lseek, dup2, getcwd, chdir, readdir, errno |
| 21 | Shell Enhancements | History, tab completion, I/O redirection, pipe chains |
| 22 | Text Editor | Nano-like editor with save/exit |
| 23 | File Descriptors | Per-process fd table, stdin/stdout/stderr, refcounting |
| 24 | Interactive I/O | Line-buffered stdin, Ctrl+C/Ctrl+D, foreground/background |
| 25 | Networking | E1000 driver, Ethernet, ARP, IPv4, ICMP ping, UDP |
| 26 | TCP/IP | Full TCP state machine with retransmission |
| 27 | Sockets API | connect/send/recv/listen/accept, httpget |
| 28 | DNS | UDP DNS client with caching |
| 29 | HTTP | HTTP client (shell command + user program) |
| 30 | Neural Networks | Matrix library, dense NN with backprop, model save/load |

## License

This project is for educational purposes.
