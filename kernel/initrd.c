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
extern uint8_t _binary_build_user_fpudemo_elf_start[];
extern uint8_t _binary_build_user_fpudemo_elf_end[];
extern uint8_t _binary_build_user_mathdemo_elf_start[];
extern uint8_t _binary_build_user_mathdemo_elf_end[];
extern uint8_t _binary_build_user_mmapdemo_elf_start[];
extern uint8_t _binary_build_user_mmapdemo_elf_end[];
extern uint8_t _binary_build_user_cat_elf_start[];
extern uint8_t _binary_build_user_cat_elf_end[];
extern uint8_t _binary_build_user_gfxdemo_elf_start[];
extern uint8_t _binary_build_user_gfxdemo_elf_end[];
extern uint8_t _binary_build_user_filedemo_elf_start[];
extern uint8_t _binary_build_user_filedemo_elf_end[];
extern uint8_t _binary_build_user_inputdemo_elf_start[];
extern uint8_t _binary_build_user_inputdemo_elf_end[];
extern uint8_t _binary_build_user_httpget_elf_start[];
extern uint8_t _binary_build_user_httpget_elf_end[];
extern uint8_t _binary_build_user_nndemo_elf_start[];
extern uint8_t _binary_build_user_nndemo_elf_end[];
extern uint8_t _binary_build_user_httpserv_elf_start[];
extern uint8_t _binary_build_user_httpserv_elf_end[];
extern uint8_t _binary_build_user_wc_elf_start[];
extern uint8_t _binary_build_user_wc_elf_end[];
extern uint8_t _binary_build_user_grep_elf_start[];
extern uint8_t _binary_build_user_grep_elf_end[];
extern uint8_t _binary_build_user_head_elf_start[];
extern uint8_t _binary_build_user_head_elf_end[];
extern uint8_t _binary_build_user_tail_elf_start[];
extern uint8_t _binary_build_user_tail_elf_end[];
extern uint8_t _binary_build_user_sort_elf_start[];
extern uint8_t _binary_build_user_sort_elf_end[];
extern uint8_t _binary_build_user_cp_elf_start[];
extern uint8_t _binary_build_user_cp_elf_end[];
extern uint8_t _binary_build_user_mv_elf_start[];
extern uint8_t _binary_build_user_mv_elf_end[];
extern uint8_t _binary_build_user_hexdump_elf_start[];
extern uint8_t _binary_build_user_hexdump_elf_end[];
extern uint8_t _binary_build_user_echoserv_elf_start[];
extern uint8_t _binary_build_user_echoserv_elf_end[];

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

static void create_test_file(const char *path, const char *content) {
    int fd = vfs_open(path, VFS_O_CREATE);
    if (fd >= 0) {
        int len = 0;
        while (content[len]) len++;
        vfs_write(fd, content, len);
        vfs_close(fd);
    }
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
    embed_file("/bin/fpudemo", _binary_build_user_fpudemo_elf_start,
               _binary_build_user_fpudemo_elf_end);
    embed_file("/bin/mathdemo", _binary_build_user_mathdemo_elf_start,
               _binary_build_user_mathdemo_elf_end);
    embed_file("/bin/mmapdemo", _binary_build_user_mmapdemo_elf_start,
               _binary_build_user_mmapdemo_elf_end);
    embed_file("/bin/cat", _binary_build_user_cat_elf_start,
               _binary_build_user_cat_elf_end);
    embed_file("/bin/gfxdemo", _binary_build_user_gfxdemo_elf_start,
               _binary_build_user_gfxdemo_elf_end);
    embed_file("/bin/filedemo", _binary_build_user_filedemo_elf_start,
               _binary_build_user_filedemo_elf_end);
    embed_file("/bin/inputdemo", _binary_build_user_inputdemo_elf_start,
               _binary_build_user_inputdemo_elf_end);
    embed_file("/bin/httpget", _binary_build_user_httpget_elf_start,
               _binary_build_user_httpget_elf_end);
    embed_file("/bin/nndemo", _binary_build_user_nndemo_elf_start,
               _binary_build_user_nndemo_elf_end);
    embed_file("/bin/httpserv", _binary_build_user_httpserv_elf_start,
               _binary_build_user_httpserv_elf_end);
    embed_file("/bin/wc", _binary_build_user_wc_elf_start,
               _binary_build_user_wc_elf_end);
    embed_file("/bin/grep", _binary_build_user_grep_elf_start,
               _binary_build_user_grep_elf_end);
    embed_file("/bin/head", _binary_build_user_head_elf_start,
               _binary_build_user_head_elf_end);
    embed_file("/bin/tail", _binary_build_user_tail_elf_start,
               _binary_build_user_tail_elf_end);
    embed_file("/bin/sort", _binary_build_user_sort_elf_start,
               _binary_build_user_sort_elf_end);
    embed_file("/bin/cp", _binary_build_user_cp_elf_start,
               _binary_build_user_cp_elf_end);
    embed_file("/bin/mv", _binary_build_user_mv_elf_start,
               _binary_build_user_mv_elf_end);
    embed_file("/bin/hexdump", _binary_build_user_hexdump_elf_start,
               _binary_build_user_hexdump_elf_end);
    embed_file("/bin/echoserv", _binary_build_user_echoserv_elf_start,
               _binary_build_user_echoserv_elf_end);

    /* Create test files for user-space programs */
    create_test_file("/hello.txt", "Hello from PlantOS!\nThis file was read by a user-space program.\n");

    /* Create /etc directory and passwd file */
    vfs_mkdir("/etc");
    create_test_file("/etc/passwd", "root:0:0:/:root\nuser:1:1:/home:user\nguest:2:2:/tmp:guest\n");

    /* Demo shell scripts */
    vfs_mkdir("/scripts");
    create_test_file("/scripts/demo.sh",
        "# PlantOS shell scripting demo\n"
        "echo === Shell Scripting Demo ===\n"
        "\n"
        "# Variables\n"
        "COUNT=3\n"
        "echo Count is $COUNT\n"
        "\n"
        "# Exit codes\n"
        "true\n"
        "echo true exits with $?\n"
        "false\n"
        "echo false exits with $?\n"
        "\n"
        "# if/else\n"
        "if test -f /hello.txt; then\n"
        "  echo /hello.txt exists\n"
        "else\n"
        "  echo /hello.txt not found\n"
        "fi\n"
        "\n"
        "if test -f /nonexistent; then\n"
        "  echo should not print\n"
        "elif test -d /etc; then\n"
        "  echo /etc is a directory\n"
        "fi\n"
        "\n"
        "# while loop with arithmetic\n"
        "I=0\n"
        "while test $I -lt $COUNT; do\n"
        "  echo Iteration $I\n"
        "  I=$(($I + 1))\n"
        "done\n"
        "\n"
        "# String comparison\n"
        "NAME=PlantOS\n"
        "if test $NAME = PlantOS; then\n"
        "  echo Name matches!\n"
        "fi\n"
        "\n"
        "echo === Demo Complete ===\n"
    );
}
