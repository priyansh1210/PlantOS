#include "fs/vfs.h"
#include "lib/string.h"
#include "lib/printf.h"

static struct vfs_fd fd_table[VFS_MAX_FDS];

void vfs_init(void) {
    memset(fd_table, 0, sizeof(fd_table));
    kprintf("[VFS] Virtual filesystem initialized\n");
}

static int alloc_fd(void) {
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        if (!fd_table[i].used) {
            fd_table[i].used = true;
            fd_table[i].offset = 0;
            fd_table[i].dir_idx = 0;
            fd_table[i].pipe = NULL;
            fd_table[i].pipe_mode = 0;
            return i;
        }
    }
    return -1;
}

struct vfs_fd *vfs_get_fd(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !fd_table[fd].used)
        return NULL;
    return &fd_table[fd];
}

int vfs_alloc_fd(void) {
    return alloc_fd();
}

/* Ramfs functions — implemented in ramfs.c */
extern struct vfs_inode *ramfs_get_inode(uint32_t idx);
extern int ramfs_resolve_path(const char *path);
extern int ramfs_create_file(const char *path);
extern int ramfs_create_dir(const char *path);
extern uint32_t ramfs_dir_entry_count(uint32_t inode);
extern struct vfs_dirent *ramfs_dir_get_entry(uint32_t inode, uint32_t idx);

int vfs_open(const char *path, int flags) {
    int ino = ramfs_resolve_path(path);

    if (ino < 0) {
        if (flags & VFS_O_CREATE) {
            ino = ramfs_create_file(path);
            if (ino < 0) return -1;
        } else {
            return -1;
        }
    }

    int fd = alloc_fd();
    if (fd < 0) return -1;

    fd_table[fd].inode = (uint32_t)ino;
    fd_table[fd].offset = 0;
    fd_table[fd].dir_idx = 0;
    return fd;
}

int vfs_close(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !fd_table[fd].used)
        return -1;
    fd_table[fd].used = false;
    return 0;
}

int vfs_read(int fd, void *buf, size_t count) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !fd_table[fd].used)
        return -1;

    struct vfs_inode *node = ramfs_get_inode(fd_table[fd].inode);
    if (!node || node->type != VFS_FILE)
        return -1;

    uint32_t off = fd_table[fd].offset;
    if (off >= node->size)
        return 0;

    uint32_t avail = node->size - off;
    if (count > avail) count = avail;

    memcpy(buf, node->data + off, count);
    fd_table[fd].offset += (uint32_t)count;
    return (int)count;
}

int vfs_write(int fd, const void *buf, size_t count) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !fd_table[fd].used)
        return -1;

    struct vfs_inode *node = ramfs_get_inode(fd_table[fd].inode);
    if (!node || node->type != VFS_FILE)
        return -1;

    uint32_t off = fd_table[fd].offset;
    uint32_t needed = off + (uint32_t)count;

    /* Grow buffer if needed */
    if (needed > node->capacity) {
        uint32_t new_cap = needed * 2;
        if (new_cap < 256) new_cap = 256;
        extern void *kmalloc(size_t);
        extern void kfree(void *);
        uint8_t *new_data = (uint8_t *)kmalloc(new_cap);
        if (!new_data) return -1;
        if (node->data) {
            memcpy(new_data, node->data, node->size);
            kfree(node->data);
        }
        node->data = new_data;
        node->capacity = new_cap;
    }

    memcpy(node->data + off, buf, count);
    fd_table[fd].offset = needed;
    if (needed > node->size)
        node->size = needed;

    return (int)count;
}

int vfs_stat(const char *path, struct vfs_stat *st) {
    int ino = ramfs_resolve_path(path);
    if (ino < 0) return -1;

    struct vfs_inode *node = ramfs_get_inode((uint32_t)ino);
    if (!node) return -1;

    st->type = node->type;
    st->size = node->size;
    st->inode = (uint32_t)ino;
    return 0;
}

int vfs_mkdir(const char *path) {
    return ramfs_create_dir(path);
}

int vfs_readdir(int fd, struct vfs_dirent *de) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !fd_table[fd].used)
        return -1;

    struct vfs_inode *node = ramfs_get_inode(fd_table[fd].inode);
    if (!node || node->type != VFS_DIR)
        return -1;

    uint32_t idx = fd_table[fd].dir_idx;
    uint32_t total = ramfs_dir_entry_count(fd_table[fd].inode);

    if (idx >= total)
        return -1;  /* No more entries */

    struct vfs_dirent *entry = ramfs_dir_get_entry(fd_table[fd].inode, idx);
    if (!entry) return -1;

    memcpy(de, entry, sizeof(*de));
    fd_table[fd].dir_idx++;
    return 0;
}
