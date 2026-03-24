#include "fs/vfs.h"
#include "mm/heap.h"
#include "lib/string.h"
#include "lib/printf.h"
#include "task/task.h"

static struct vfs_inode inode_table[VFS_MAX_INODES];

struct vfs_inode *ramfs_get_inode(uint32_t idx) {
    if (idx >= VFS_MAX_INODES || !inode_table[idx].used)
        return NULL;
    return &inode_table[idx];
}

static int alloc_inode(void) {
    for (int i = 0; i < VFS_MAX_INODES; i++) {
        if (!inode_table[i].used) {
            memset(&inode_table[i], 0, sizeof(inode_table[i]));
            inode_table[i].used = true;
            return i;
        }
    }
    return -1;
}

/*
 * Directory data format: array of vfs_dirent structs.
 * size = number of entries * sizeof(vfs_dirent)
 */

static int dir_add_entry(uint32_t dir_ino, const char *name, uint32_t child_ino) {
    struct vfs_inode *dir = &inode_table[dir_ino];
    if (dir->type != VFS_DIR) return -1;

    uint32_t entry_count = dir->size / sizeof(struct vfs_dirent);
    uint32_t needed = (entry_count + 1) * sizeof(struct vfs_dirent);

    if (needed > dir->capacity) {
        uint32_t new_cap = needed * 2;
        if (new_cap < 512) new_cap = 512;
        uint8_t *new_data = (uint8_t *)kmalloc(new_cap);
        if (!new_data) return -1;
        if (dir->data) {
            memcpy(new_data, dir->data, dir->size);
            kfree(dir->data);
        }
        dir->data = new_data;
        dir->capacity = new_cap;
    }

    struct vfs_dirent *de = (struct vfs_dirent *)(dir->data + dir->size);
    memset(de, 0, sizeof(*de));
    strncpy(de->name, name, VFS_NAME_MAX - 1);
    de->name[VFS_NAME_MAX - 1] = '\0';
    de->inode = child_ino;
    dir->size = needed;

    return 0;
}

uint32_t ramfs_dir_entry_count(uint32_t inode) {
    struct vfs_inode *node = ramfs_get_inode(inode);
    if (!node || node->type != VFS_DIR) return 0;
    return node->size / sizeof(struct vfs_dirent);
}

struct vfs_dirent *ramfs_dir_get_entry(uint32_t inode, uint32_t idx) {
    struct vfs_inode *node = ramfs_get_inode(inode);
    if (!node || node->type != VFS_DIR) return NULL;
    uint32_t count = node->size / sizeof(struct vfs_dirent);
    if (idx >= count) return NULL;
    return (struct vfs_dirent *)(node->data) + idx;
}

/*
 * Path resolution: walk from root (inode 0) following '/' separators.
 * Returns inode index or -1.
 */
int ramfs_resolve_path(const char *path) {
    if (!path || path[0] != '/') return -1;

    /* Root itself */
    if (path[1] == '\0') return 0;

    uint32_t cur = 0;  /* Start at root inode */
    const char *p = path + 1;

    while (*p) {
        /* Skip leading slashes */
        while (*p == '/') p++;
        if (*p == '\0') break;

        /* Extract component name */
        char component[VFS_NAME_MAX];
        int i = 0;
        while (*p && *p != '/' && i < VFS_NAME_MAX - 1) {
            component[i++] = *p++;
        }
        component[i] = '\0';

        /* Look up in current directory */
        struct vfs_inode *dir = ramfs_get_inode(cur);
        if (!dir || dir->type != VFS_DIR) return -1;

        uint32_t count = dir->size / sizeof(struct vfs_dirent);
        struct vfs_dirent *entries = (struct vfs_dirent *)dir->data;
        bool found = false;

        for (uint32_t j = 0; j < count; j++) {
            if (strcmp(entries[j].name, component) == 0) {
                cur = entries[j].inode;
                found = true;
                break;
            }
        }

        if (!found) return -1;
    }

    return (int)cur;
}

/*
 * Split a path into parent directory path and basename.
 * Returns 0 on success.
 */
static int split_path(const char *path, char *parent, char *name) {
    int len = strlen(path);
    if (len < 2 || path[0] != '/') return -1;

    /* Find last '/' */
    int last_slash = 0;
    for (int i = len - 1; i > 0; i--) {
        if (path[i] == '/') {
            last_slash = i;
            break;
        }
    }

    /* Parent path */
    if (last_slash == 0) {
        parent[0] = '/';
        parent[1] = '\0';
    } else {
        memcpy(parent, path, last_slash);
        parent[last_slash] = '\0';
    }

    /* Basename */
    strcpy(name, path + last_slash + 1);
    return 0;
}

int ramfs_create_file(const char *path) {
    char parent_path[VFS_PATH_MAX];
    char name[VFS_NAME_MAX];

    if (split_path(path, parent_path, name) < 0) return -1;

    int parent_ino = ramfs_resolve_path(parent_path);
    if (parent_ino < 0) return -1;

    /* Check if already exists */
    int existing = ramfs_resolve_path(path);
    if (existing >= 0) return existing;

    int ino = alloc_inode();
    if (ino < 0) return -1;

    inode_table[ino].type = VFS_FILE;
    inode_table[ino].size = 0;
    inode_table[ino].data = NULL;
    inode_table[ino].capacity = 0;
    inode_table[ino].mode = 0644; /* rw-r--r-- */
    struct task *cur = task_current();
    if (cur) { inode_table[ino].uid = cur->uid; inode_table[ino].gid = cur->gid; }

    if (dir_add_entry((uint32_t)parent_ino, name, (uint32_t)ino) < 0) {
        inode_table[ino].used = false;
        return -1;
    }

    return ino;
}

int ramfs_create_dir(const char *path) {
    char parent_path[VFS_PATH_MAX];
    char name[VFS_NAME_MAX];

    if (split_path(path, parent_path, name) < 0) return -1;

    int parent_ino = ramfs_resolve_path(parent_path);
    if (parent_ino < 0) return -1;

    /* Check if already exists */
    int existing = ramfs_resolve_path(path);
    if (existing >= 0) return -1;  /* Already exists */

    int ino = alloc_inode();
    if (ino < 0) return -1;

    inode_table[ino].type = VFS_DIR;
    inode_table[ino].size = 0;
    inode_table[ino].data = NULL;
    inode_table[ino].capacity = 0;
    inode_table[ino].mode = 0755; /* rwxr-xr-x */
    struct task *cur = task_current();
    if (cur) { inode_table[ino].uid = cur->uid; inode_table[ino].gid = cur->gid; }

    if (dir_add_entry((uint32_t)parent_ino, name, (uint32_t)ino) < 0) {
        inode_table[ino].used = false;
        return -1;
    }

    return ino;
}

int ramfs_unlink(const char *path) {
    char parent_path[VFS_PATH_MAX];
    char name[VFS_NAME_MAX];

    if (split_path(path, parent_path, name) < 0) return -1;

    int parent_ino = ramfs_resolve_path(parent_path);
    if (parent_ino < 0) return -1;

    int target_ino = ramfs_resolve_path(path);
    if (target_ino < 0) return -1;

    /* Don't delete directories */
    struct vfs_inode *target = ramfs_get_inode((uint32_t)target_ino);
    if (!target || target->type != VFS_FILE) return -1;

    /* Remove entry from parent directory */
    struct vfs_inode *parent = ramfs_get_inode((uint32_t)parent_ino);
    if (!parent || parent->type != VFS_DIR) return -1;

    uint32_t count = parent->size / sizeof(struct vfs_dirent);
    struct vfs_dirent *entries = (struct vfs_dirent *)parent->data;

    for (uint32_t i = 0; i < count; i++) {
        if (strcmp(entries[i].name, name) == 0) {
            /* Shift remaining entries down */
            for (uint32_t j = i; j + 1 < count; j++)
                entries[j] = entries[j + 1];
            parent->size -= sizeof(struct vfs_dirent);
            break;
        }
    }

    /* Free inode data */
    if (target->data) {
        kfree(target->data);
        target->data = NULL;
    }
    target->used = false;
    target->size = 0;
    target->capacity = 0;

    return 0;
}

void ramfs_init(void) {
    memset(inode_table, 0, sizeof(inode_table));

    /* Inode 0 = root directory */
    inode_table[0].used = true;
    inode_table[0].type = VFS_DIR;
    inode_table[0].size = 0;
    inode_table[0].data = NULL;
    inode_table[0].capacity = 0;
    inode_table[0].uid = 0;
    inode_table[0].gid = 0;
    inode_table[0].mode = 0755;

    kprintf("[RAMFS] In-memory filesystem initialized\n");
}
