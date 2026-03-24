#include "fs/vfs.h"
#include "fs/fat.h"
#include "fs/pipe.h"
#include "lib/string.h"
#include "lib/printf.h"
#include "mm/heap.h"
#include "task/task.h"

static struct vfs_fd fd_table[VFS_MAX_FDS];

/* FAT mount point (e.g., "/disk") */
static char fat_mpoint[64];
static bool fat_mounted_flag = false;

void vfs_init(void) {
    memset(fd_table, 0, sizeof(fd_table));
    fat_mpoint[0] = '\0';
    fat_mounted_flag = false;
    kprintf("[VFS] Virtual filesystem initialized\n");
}

static int alloc_fd(void) {
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        if (!fd_table[i].used) {
            fd_table[i].used = true;
            fd_table[i].refcount = 1;
            fd_table[i].offset = 0;
            fd_table[i].dir_idx = 0;
            fd_table[i].pipe = NULL;
            fd_table[i].pipe_mode = 0;
            fd_table[i].fat_fd = -1;
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

/* ---- FAT mount routing ---- */

/* Check if path belongs to FAT mount and return the sub-path within FAT.
 * E.g., mount="/disk", path="/disk/foo.txt" → returns "/foo.txt"
 * Returns NULL if path is not under the mount point. */
static const char *fat_subpath(const char *path) {
    if (!fat_mounted_flag || !fat_mpoint[0]) return NULL;
    int mlen = strlen(fat_mpoint);
    if (strncmp(path, fat_mpoint, mlen) != 0) return NULL;
    if (path[mlen] == '\0') return "/";
    if (path[mlen] == '/') return path + mlen;
    return NULL;
}

int vfs_mount_fat(const char *mount_point) {
    if (fat_mounted_flag) {
        kprintf("[VFS] FAT already mounted\n");
        return -1;
    }
    strncpy(fat_mpoint, mount_point, sizeof(fat_mpoint) - 1);
    fat_mpoint[sizeof(fat_mpoint) - 1] = '\0';

    /* Remove trailing slash */
    int len = strlen(fat_mpoint);
    if (len > 1 && fat_mpoint[len - 1] == '/')
        fat_mpoint[len - 1] = '\0';

    fat_mounted_flag = true;
    kprintf("[VFS] FAT32 mounted at '%s'\n", fat_mpoint);
    return 0;
}

void vfs_unmount_fat(void) {
    if (!fat_mounted_flag) return;
    fat_unmount();
    fat_mpoint[0] = '\0';
    fat_mounted_flag = false;
}

bool vfs_fat_mounted(void) {
    return fat_mounted_flag;
}

const char *vfs_fat_mount_point(void) {
    return fat_mpoint;
}

/* Ramfs functions — implemented in ramfs.c */
extern struct vfs_inode *ramfs_get_inode(uint32_t idx);
extern int ramfs_resolve_path(const char *path);
extern int ramfs_create_file(const char *path);
extern int ramfs_create_dir(const char *path);
extern int ramfs_unlink(const char *path);
extern uint32_t ramfs_dir_entry_count(uint32_t inode);
extern struct vfs_dirent *ramfs_dir_get_entry(uint32_t inode, uint32_t idx);

/* Permission check: returns 0 if access allowed, -1 if denied.
 * want_bits: 4=read, 2=write, 1=execute (standard POSIX octal style) */
static int vfs_check_perm(struct vfs_inode *node, int want_bits) {
    struct task *t = task_current();
    if (!t) return 0;          /* kernel context — allow */
    if (t->uid == 0) return 0; /* root — allow everything */

    uint16_t mode = node->mode;
    int bits;
    if (t->uid == node->uid)
        bits = (mode >> 6) & 7; /* owner */
    else if (t->gid == node->gid)
        bits = (mode >> 3) & 7; /* group */
    else
        bits = mode & 7;        /* other */

    return ((bits & want_bits) == want_bits) ? 0 : -1;
}

int vfs_open(const char *path, int flags) {
    /* Check FAT mount first */
    const char *fpath = fat_subpath(path);
    if (fpath) {
        int ffd = fat_open(fpath, flags);
        if (ffd < 0) return -1;

        int fd = alloc_fd();
        if (fd < 0) { fat_close(ffd); return -1; }

        fd_table[fd].fat_fd = ffd;
        return fd;
    }

    /* Ramfs path */
    int ino = ramfs_resolve_path(path);

    if (ino < 0) {
        if (flags & VFS_O_CREATE) {
            ino = ramfs_create_file(path);
            if (ino < 0) return -1;
        } else {
            return -1;
        }
    } else {
        /* Permission check on existing file */
        struct vfs_inode *node = ramfs_get_inode((uint32_t)ino);
        if (node) {
            int want = 4; /* read */
            if (flags & (VFS_O_CREATE | VFS_O_TRUNC))
                want |= 2; /* write */
            if (vfs_check_perm(node, want) < 0)
                return -1; /* EACCES */
        }
    }

    int fd = alloc_fd();
    if (fd < 0) return -1;

    fd_table[fd].inode = (uint32_t)ino;
    fd_table[fd].offset = 0;
    fd_table[fd].dir_idx = 0;

    /* O_TRUNC: truncate ramfs file to zero */
    if (flags & VFS_O_TRUNC) {
        struct vfs_inode *node = ramfs_get_inode((uint32_t)ino);
        if (node && node->type == VFS_FILE) {
            node->size = 0;
        }
    }

    return fd;
}

int vfs_close(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !fd_table[fd].used)
        return -1;

    /* Decrement refcount; only actually close when it hits 0 */
    if (fd_table[fd].refcount > 1) {
        fd_table[fd].refcount--;
        return 0;
    }

    if (fd_table[fd].fat_fd >= 0) {
        fat_close(fd_table[fd].fat_fd);
        fd_table[fd].fat_fd = -1;
    }

    fd_table[fd].refcount = 0;
    fd_table[fd].used = false;
    return 0;
}

void vfs_fd_addref(int fd) {
    if (fd >= 0 && fd < VFS_MAX_FDS && fd_table[fd].used)
        fd_table[fd].refcount++;
}

int vfs_read(int fd, void *buf, size_t count) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !fd_table[fd].used)
        return -1;

    if (fd_table[fd].fat_fd >= 0)
        return fat_read(fd_table[fd].fat_fd, buf, count);

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

    if (fd_table[fd].fat_fd >= 0)
        return fat_write(fd_table[fd].fat_fd, buf, count);

    struct vfs_inode *node = ramfs_get_inode(fd_table[fd].inode);
    if (!node || node->type != VFS_FILE)
        return -1;

    uint32_t off = fd_table[fd].offset;
    uint32_t needed = off + (uint32_t)count;

    /* Grow buffer if needed */
    if (needed > node->capacity) {
        uint32_t new_cap = needed * 2;
        if (new_cap < 256) new_cap = 256;
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
    const char *fpath = fat_subpath(path);
    if (fpath) {
        uint32_t size;
        uint8_t is_dir;
        if (fat_stat(fpath, &size, &is_dir) < 0) return -1;
        st->type = is_dir ? VFS_DIR : VFS_FILE;
        st->size = size;
        st->inode = 0;
        st->uid = 0;
        st->gid = 0;
        st->mode = is_dir ? 0755 : 0644;
        return 0;
    }

    int ino = ramfs_resolve_path(path);
    if (ino < 0) return -1;

    struct vfs_inode *node = ramfs_get_inode((uint32_t)ino);
    if (!node) return -1;

    st->type = node->type;
    st->size = node->size;
    st->inode = (uint32_t)ino;
    st->uid = node->uid;
    st->gid = node->gid;
    st->mode = node->mode;
    return 0;
}

int vfs_mkdir(const char *path) {
    const char *fpath = fat_subpath(path);
    if (fpath)
        return fat_mkdir(fpath);

    return ramfs_create_dir(path);
}

int vfs_unlink(const char *path) {
    const char *fpath = fat_subpath(path);
    if (fpath)
        return fat_unlink(fpath);

    /* Permission check: need write on file */
    int ino = ramfs_resolve_path(path);
    if (ino >= 0) {
        struct vfs_inode *node = ramfs_get_inode((uint32_t)ino);
        if (node && vfs_check_perm(node, 2) < 0)
            return -1; /* EACCES */
    }

    return ramfs_unlink(path);
}

int vfs_readdir(int fd, struct vfs_dirent *de) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !fd_table[fd].used)
        return -1;

    if (fd_table[fd].fat_fd >= 0) {
        char name[60];
        uint32_t size;
        uint8_t is_dir;
        if (fat_readdir(fd_table[fd].fat_fd, name, &size, &is_dir) < 0)
            return -1;
        strncpy(de->name, name, VFS_NAME_MAX - 1);
        de->name[VFS_NAME_MAX - 1] = '\0';
        de->inode = 0; /* FAT doesn't use inodes in our model */
        return 0;
    }

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

int vfs_lseek(int fd, int offset, int whence) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !fd_table[fd].used)
        return -1;

    /* Not supported on pipes */
    if (fd_table[fd].pipe)
        return -1;

    /* For FAT fds, we don't support seek yet — only ramfs */
    if (fd_table[fd].fat_fd >= 0)
        return -1;

    struct vfs_inode *node = ramfs_get_inode(fd_table[fd].inode);
    if (!node || node->type != VFS_FILE)
        return -1;

    int new_off;
    switch (whence) {
    case VFS_SEEK_SET: new_off = offset; break;
    case VFS_SEEK_CUR: new_off = (int)fd_table[fd].offset + offset; break;
    case VFS_SEEK_END: new_off = (int)node->size + offset; break;
    default: return -1;
    }

    if (new_off < 0) new_off = 0;
    fd_table[fd].offset = (uint32_t)new_off;
    return new_off;
}

int vfs_dup2(int oldfd, int newfd) {
    if (oldfd < 0 || oldfd >= VFS_MAX_FDS || !fd_table[oldfd].used)
        return -1;
    if (newfd < 0 || newfd >= VFS_MAX_FDS)
        return -1;

    /* Close newfd if open */
    if (fd_table[newfd].used)
        vfs_close(newfd);

    /* Copy fd entry and share refcount */
    fd_table[newfd] = fd_table[oldfd];
    fd_table[newfd].refcount = 1;
    fd_table[oldfd].refcount++;
    return newfd;
}

int vfs_close_fd(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !fd_table[fd].used)
        return -1;

    /* Check refcount first */
    if (fd_table[fd].refcount > 1) {
        fd_table[fd].refcount--;
        return 0;
    }

    /* Actually release the resource */
    if (fd_table[fd].pipe) {
        /* Pipe close path */
        struct pipe *p = fd_table[fd].pipe;
        if (fd_table[fd].pipe_mode == 1) { /* PIPE_READ */
            p->readers--;
            if (p->readers == 0)
                task_wake_all(p);
        } else if (fd_table[fd].pipe_mode == 2) { /* PIPE_WRITE */
            p->writers--;
            if (p->writers == 0)
                task_wake_all(p);
        }
        if (p->readers == 0 && p->writers == 0)
            p->used = false;
        fd_table[fd].pipe = NULL;
        fd_table[fd].pipe_mode = 0;
    }

    if (fd_table[fd].fat_fd >= 0) {
        fat_close(fd_table[fd].fat_fd);
        fd_table[fd].fat_fd = -1;
    }

    fd_table[fd].refcount = 0;
    fd_table[fd].used = false;
    return 0;
}
