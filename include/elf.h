#ifndef ELF_H
#define ELF_H

#include <plantos/types.h>

/* ELF magic */
#define ELF_MAGIC  0x464C457F  /* "\x7FELF" as little-endian uint32 */

/* ELF class */
#define ELFCLASS64 2

/* ELF type */
#define ET_EXEC    2

/* ELF machine */
#define EM_X86_64  62

/* Program header types */
#define PT_NULL    0
#define PT_LOAD    1

/* Program header flags */
#define PF_X       0x1
#define PF_W       0x2
#define PF_R       0x4

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed)) Elf64_Phdr;

#endif
