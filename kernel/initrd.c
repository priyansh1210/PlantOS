#include "kernel/initrd.h"
#include "fs/vfs.h"
#include "lib/printf.h"

extern uint8_t _binary_build_user_hello_elf_start[];
extern uint8_t _binary_build_user_hello_elf_end[];
extern uint8_t _binary_build_user_sigdemo_elf_start[];
extern uint8_t _binary_build_user_sigdemo_elf_end[];
extern uint8_t _binary_build_user_forkdemo_elf_start[];
extern uint8_t _binary_build_user_forkdemo_elf_end[];
extern uint8_t _binary_build_user_mallocdemo_elf_start[];
extern uint8_t _binary_build_user_mallocdemo_elf_end[];

static void embed_file(const char *path, uint8_t *start, uint8_t *end) {
    uint32_t size = (uint32_t)(end - start);
    int fd = vfs_open(path, VFS_O_CREATE);
    if (fd < 0) {
        kprintf("[INITRD] Failed to create %s\n", path);
        return;
    }
    vfs_write(fd, start, size);
    vfs_close(fd);
    kprintf("[INITRD] Embedded '%s' (%u bytes)\n", path, size);
}

void initrd_init(void) {
    vfs_mkdir("/bin");

    embed_file("/bin/hello", _binary_build_user_hello_elf_start,
               _binary_build_user_hello_elf_end);
    embed_file("/bin/sigdemo", _binary_build_user_sigdemo_elf_start,
               _binary_build_user_sigdemo_elf_end);
    embed_file("/bin/forkdemo", _binary_build_user_forkdemo_elf_start,
               _binary_build_user_forkdemo_elf_end);
    embed_file("/bin/mallocdemo", _binary_build_user_mallocdemo_elf_start,
               _binary_build_user_mallocdemo_elf_end);
}
