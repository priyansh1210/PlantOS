#ifndef FS_VFS_H
#define FS_VFS_H

#include <plantos/types.h>

#define VFS_MAX_INODES  64
#define VFS_MAX_FDS     64
#define VFS_NAME_MAX    60
#define VFS_PATH_MAX    256

#define VFS_FILE  1
#define VFS_DIR   2

#define VFS_O_CREATE  0x01
#define VFS_O_TRUNC   0x02

struct vfs_inode {
    uint32_t  type;       /* VFS_FILE or VFS_DIR */
    uint32_t  size;
    uint8_t  *data;       /* kmalloc'd data (files: raw bytes, dirs: dirent array) */
    uint32_t  capacity;
    bool      used;
    uint16_t  uid;        /* Owner user ID */
    uint16_t  gid;        /* Owner group ID */
    uint16_t  mode;       /* Permission bits (rwxrwxrwx) */
};

struct vfs_dirent {
    char     name[VFS_NAME_MAX];
    uint32_t inode;
};

struct vfs_stat {
    uint32_t type;
    uint32_t size;
    uint32_t inode;
    uint16_t uid;
    uint16_t gid;
    uint16_t mode;
};

/* Forward declare pipe */
struct pipe;

/* File descriptor */
struct vfs_fd {
    bool     used;
    uint32_t refcount;   /* reference count (for shared fds after fork) */
    uint32_t inode;
    uint32_t offset;     /* read/write position */
    uint32_t dir_idx;    /* readdir position */
    struct pipe *pipe;   /* Non-NULL if this FD is a pipe end */
    uint8_t  pipe_mode;  /* 0=not pipe, 1=read end, 2=write end */
    int      fat_fd;     /* >=0 if this FD is backed by FAT; -1 otherwise */
};

/* VFS API */
void vfs_init(void);
int  vfs_open(const char *path, int flags);
int  vfs_close(int fd);
int  vfs_read(int fd, void *buf, size_t count);
int  vfs_write(int fd, const void *buf, size_t count);
int  vfs_stat(const char *path, struct vfs_stat *st);
int  vfs_mkdir(const char *path);
int  vfs_readdir(int fd, struct vfs_dirent *de);

/* FD table access (for pipe integration) */
struct vfs_fd *vfs_get_fd(int fd);
int vfs_alloc_fd(void);

/* Delete a file */
int vfs_unlink(const char *path);

/* Seek */
#define VFS_SEEK_SET 0
#define VFS_SEEK_CUR 1
#define VFS_SEEK_END 2
int vfs_lseek(int fd, int offset, int whence);

/* Duplicate fd */
int vfs_dup2(int oldfd, int newfd);

/* Increment refcount on an OFD (for fork fd table copy) */
void vfs_fd_addref(int fd);

/* Unified close — handles pipes and regular files, respects refcount */
int vfs_close_fd(int fd);

/* Ramfs init */
void ramfs_init(void);

/* Mount FAT32 filesystem at a given path (e.g., "/disk") */
int  vfs_mount_fat(const char *mount_point);
void vfs_unmount_fat(void);
bool vfs_fat_mounted(void);
const char *vfs_fat_mount_point(void);

#endif
