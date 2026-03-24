#include "fs/fat.h"
#include "fs/bcache.h"
#include "drivers/ata.h"
#include "mm/heap.h"
#include "lib/string.h"
#include "lib/printf.h"

static struct fat_mount mount;
static struct fat_file  file_table[FAT_MAX_FILES];
static uint8_t sector_buf[512];

/* ---- Low-level helpers (via buffer cache) ---- */

static int read_sector(uint32_t lba, void *buf) {
    uint8_t *cached = bcache_read(lba);
    if (!cached) return -1;
    memcpy(buf, cached, 512);
    return 0;
}

static int write_sector(uint32_t lba, const void *buf) {
    return bcache_write(lba, buf);
}

static uint32_t cluster_to_lba(uint32_t cluster) {
    return mount.data_start_lba + (cluster - 2) * mount.sectors_per_cluster;
}

/* Read a full cluster into buf (must be sectors_per_cluster * 512 bytes) */
static int read_cluster(uint32_t cluster, void *buf) {
    uint32_t lba = cluster_to_lba(cluster);
    uint8_t *dst = (uint8_t *)buf;
    for (uint32_t i = 0; i < mount.sectors_per_cluster; i++) {
        uint8_t *cached = bcache_read(lba + i);
        if (!cached) return -1;
        memcpy(dst + i * 512, cached, 512);
    }
    return 0;
}

static int write_cluster(uint32_t cluster, const void *buf) {
    uint32_t lba = cluster_to_lba(cluster);
    const uint8_t *src = (const uint8_t *)buf;
    for (uint32_t i = 0; i < mount.sectors_per_cluster; i++) {
        if (bcache_write(lba + i, src + i * 512) < 0) return -1;
    }
    return 0;
}

/* ---- FAT table operations ---- */

static uint32_t fat_read_entry(uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = mount.fat_start_lba + (fat_offset / 512);
    uint32_t entry_offset = fat_offset % 512;

    if (read_sector(fat_sector, sector_buf) < 0) return FAT32_EOC;

    uint32_t val;
    memcpy(&val, sector_buf + entry_offset, 4);
    return val & 0x0FFFFFFF;
}

static int fat_write_entry(uint32_t cluster, uint32_t value) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = mount.fat_start_lba + (fat_offset / 512);
    uint32_t entry_offset = fat_offset % 512;

    if (read_sector(fat_sector, sector_buf) < 0) return -1;

    /* Preserve top 4 bits */
    uint32_t existing;
    memcpy(&existing, sector_buf + entry_offset, 4);
    value = (existing & 0xF0000000) | (value & 0x0FFFFFFF);
    memcpy(sector_buf + entry_offset, &value, 4);

    return write_sector(fat_sector, sector_buf);
}

/* Allocate a free cluster, mark it as EOC, zero it */
static uint32_t fat_alloc_cluster(void) {
    for (uint32_t c = 2; c < mount.total_clusters + 2; c++) {
        if (fat_read_entry(c) == FAT32_FREE) {
            fat_write_entry(c, FAT32_EOC);
            /* Zero the cluster */
            uint32_t cluster_bytes = mount.sectors_per_cluster * 512;
            uint8_t *zbuf = kmalloc(cluster_bytes);
            if (zbuf) {
                memset(zbuf, 0, cluster_bytes);
                write_cluster(c, zbuf);
                kfree(zbuf);
            }
            return c;
        }
    }
    return 0; /* No free clusters */
}

/* Append a new cluster to the chain ending at `last` */
static uint32_t fat_extend_chain(uint32_t last) {
    uint32_t new_cluster = fat_alloc_cluster();
    if (new_cluster == 0) return 0;
    fat_write_entry(last, new_cluster);
    return new_cluster;
}

/* ---- 8.3 name conversion ---- */

static void fat_name_to_83(const char *name, char out[11]) {
    memset(out, ' ', 11);

    /* Find the dot */
    int dot = -1;
    for (int i = 0; name[i]; i++) {
        if (name[i] == '.') dot = i;
    }

    /* Copy base name (up to 8 chars) */
    int limit = dot >= 0 ? dot : (int)strlen(name);
    if (limit > 8) limit = 8;
    for (int i = 0; i < limit; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c -= 32; /* Uppercase */
        out[i] = c;
    }

    /* Copy extension (up to 3 chars) */
    if (dot >= 0) {
        const char *ext = name + dot + 1;
        for (int i = 0; i < 3 && ext[i]; i++) {
            char c = ext[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            out[8 + i] = c;
        }
    }
}

static void fat_83_to_name(const struct fat_dirent *de, char *out) {
    int pos = 0;

    /* Copy base name, trimming trailing spaces */
    for (int i = 0; i < 8 && de->name[i] != ' '; i++) {
        char c = de->name[i];
        if (c >= 'A' && c <= 'Z') c += 32; /* Lowercase */
        out[pos++] = c;
    }

    /* Add extension if present */
    if (de->ext[0] != ' ') {
        out[pos++] = '.';
        for (int i = 0; i < 3 && de->ext[i] != ' '; i++) {
            char c = de->ext[i];
            if (c >= 'A' && c <= 'Z') c += 32;
            out[pos++] = c;
        }
    }

    out[pos] = '\0';
}

/* ---- Directory traversal ---- */

/* Read all entries from a directory cluster chain into a heap buffer.
 * Returns total number of 32-byte entries, sets *out_buf. Caller must kfree. */
static int fat_read_dir(uint32_t cluster, struct fat_dirent **out_buf) {
    uint32_t cluster_bytes = mount.sectors_per_cluster * 512;
    uint32_t entries_per_cluster = cluster_bytes / sizeof(struct fat_dirent);

    /* Count clusters in chain */
    uint32_t chain_len = 0;
    uint32_t c = cluster;
    while (c >= 2 && c < FAT32_EOC) {
        chain_len++;
        c = fat_read_entry(c);
        if (chain_len > 10000) break; /* Safety */
    }

    uint32_t total_entries = chain_len * entries_per_cluster;
    struct fat_dirent *buf = kmalloc(total_entries * sizeof(struct fat_dirent));
    if (!buf) return -1;

    c = cluster;
    uint32_t idx = 0;
    while (c >= 2 && c < FAT32_EOC) {
        if (read_cluster(c, (uint8_t *)buf + idx * cluster_bytes) < 0) {
            kfree(buf);
            return -1;
        }
        idx++;
        c = fat_read_entry(c);
    }

    *out_buf = buf;
    return (int)total_entries;
}

/* Find a directory entry by 8.3 name in directory starting at `dir_cluster`.
 * Returns entry index (within contiguous dir data), fills *de if found. */
static int fat_find_in_dir(uint32_t dir_cluster, const char name83[11],
                           struct fat_dirent *de, uint32_t *entry_index) {
    struct fat_dirent *buf;
    int count = fat_read_dir(dir_cluster, &buf);
    if (count < 0) return -1;

    for (int i = 0; i < count; i++) {
        if ((uint8_t)buf[i].name[0] == 0x00) break; /* No more entries */
        if ((uint8_t)buf[i].name[0] == 0xE5) continue; /* Deleted */
        if (buf[i].attr == FAT_ATTR_LFN) continue;      /* Skip LFN */

        if (memcmp(buf[i].name, name83, 8) == 0 && memcmp(buf[i].ext, name83 + 8, 3) == 0) {
            if (de) *de = buf[i];
            if (entry_index) *entry_index = (uint32_t)i;
            kfree(buf);
            return 0;
        }
    }

    kfree(buf);
    return -1; /* Not found */
}

/* Path resolution: returns the starting cluster and fills de for the last component.
 * For directories, the "file size" is 0 — check attr for FAT_ATTR_DIRECTORY.
 * Returns 0 on success, -1 on not found. */
static int fat_resolve_path(const char *path, struct fat_dirent *de,
                            uint32_t *parent_cluster) {
    if (!path || path[0] != '/') return -1;
    if (path[1] == '\0') {
        /* Root directory */
        if (de) {
            memset(de, 0, sizeof(*de));
            de->attr = FAT_ATTR_DIRECTORY;
            de->cluster_lo = mount.root_cluster & 0xFFFF;
            de->cluster_hi = (mount.root_cluster >> 16) & 0xFFFF;
        }
        if (parent_cluster) *parent_cluster = 0;
        return 0;
    }

    uint32_t cur_cluster = mount.root_cluster;
    const char *p = path + 1;

    while (*p) {
        while (*p == '/') p++;
        if (*p == '\0') break;

        /* Extract component */
        char component[256];
        int i = 0;
        while (*p && *p != '/' && i < 255) {
            component[i++] = *p++;
        }
        component[i] = '\0';

        char name83[11];
        fat_name_to_83(component, name83);

        struct fat_dirent found;
        if (fat_find_in_dir(cur_cluster, name83, &found, NULL) < 0)
            return -1;

        if (parent_cluster) *parent_cluster = cur_cluster;
        if (de) *de = found;

        uint32_t next = ((uint32_t)found.cluster_hi << 16) | found.cluster_lo;
        if (*p && (found.attr & FAT_ATTR_DIRECTORY)) {
            cur_cluster = next;
        } else if (*p && !(found.attr & FAT_ATTR_DIRECTORY)) {
            return -1; /* Non-dir in middle of path */
        }
    }

    return 0;
}

/* Split path into parent path + basename */
static int fat_split_path(const char *path, char *parent, char *name) {
    int len = strlen(path);
    if (len < 2 || path[0] != '/') return -1;

    int last_slash = 0;
    for (int i = len - 1; i > 0; i--) {
        if (path[i] == '/') { last_slash = i; break; }
    }

    if (last_slash == 0) {
        parent[0] = '/'; parent[1] = '\0';
    } else {
        memcpy(parent, path, last_slash);
        parent[last_slash] = '\0';
    }
    strcpy(name, path + last_slash + 1);
    return 0;
}

/* Add a new directory entry to a directory cluster chain */
static int fat_add_dir_entry(uint32_t dir_cluster, const struct fat_dirent *entry) {
    uint32_t cluster_bytes = mount.sectors_per_cluster * 512;
    uint32_t entries_per_cluster = cluster_bytes / sizeof(struct fat_dirent);
    uint8_t *cbuf = kmalloc(cluster_bytes);
    if (!cbuf) return -1;

    uint32_t c = dir_cluster;
    while (c >= 2 && c < FAT32_EOC) {
        if (read_cluster(c, cbuf) < 0) { kfree(cbuf); return -1; }

        struct fat_dirent *entries = (struct fat_dirent *)cbuf;
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            if ((uint8_t)entries[i].name[0] == 0x00 ||
                (uint8_t)entries[i].name[0] == 0xE5) {
                entries[i] = *entry;
                if (write_cluster(c, cbuf) < 0) { kfree(cbuf); return -1; }
                kfree(cbuf);
                return 0;
            }
        }

        uint32_t next = fat_read_entry(c);
        if (next >= FAT32_EOC) {
            /* Extend directory chain */
            uint32_t new_c = fat_extend_chain(c);
            if (new_c == 0) { kfree(cbuf); return -1; }
            /* Write entry at start of new cluster */
            memset(cbuf, 0, cluster_bytes);
            struct fat_dirent *ne = (struct fat_dirent *)cbuf;
            ne[0] = *entry;
            if (write_cluster(new_c, cbuf) < 0) { kfree(cbuf); return -1; }
            kfree(cbuf);
            return 0;
        }
        c = next;
    }

    kfree(cbuf);
    return -1;
}

/* Update a directory entry on disk (for file size changes) */
static int fat_update_dir_entry(uint32_t dir_cluster, uint32_t entry_index,
                                const struct fat_dirent *entry) {
    uint32_t cluster_bytes = mount.sectors_per_cluster * 512;
    uint32_t entries_per_cluster = cluster_bytes / sizeof(struct fat_dirent);
    uint8_t *cbuf = kmalloc(cluster_bytes);
    if (!cbuf) return -1;

    uint32_t c = dir_cluster;
    uint32_t remaining = entry_index;
    while (c >= 2 && c < FAT32_EOC) {
        if (remaining < entries_per_cluster) {
            if (read_cluster(c, cbuf) < 0) { kfree(cbuf); return -1; }
            struct fat_dirent *entries = (struct fat_dirent *)cbuf;
            entries[remaining] = *entry;
            if (write_cluster(c, cbuf) < 0) { kfree(cbuf); return -1; }
            kfree(cbuf);
            return 0;
        }
        remaining -= entries_per_cluster;
        c = fat_read_entry(c);
    }

    kfree(cbuf);
    return -1;
}

/* ---- File descriptor helpers ---- */

static int fat_alloc_fd(void) {
    for (int i = 0; i < FAT_MAX_FILES; i++) {
        if (!file_table[i].used) {
            memset(&file_table[i], 0, sizeof(file_table[i]));
            file_table[i].used = true;
            return i;
        }
    }
    return -1;
}

/* Walk the cluster chain to find the n-th cluster */
static uint32_t fat_follow_chain(uint32_t start, uint32_t n) {
    uint32_t c = start;
    for (uint32_t i = 0; i < n && c >= 2 && c < FAT32_EOC; i++) {
        c = fat_read_entry(c);
    }
    return c;
}

/* ---- Public API ---- */

void fat_init(void) {
    memset(&mount, 0, sizeof(mount));
    memset(file_table, 0, sizeof(file_table));
}

int fat_mount(uint32_t partition_lba) {
    if (mount.mounted) {
        kprintf("[FAT] Already mounted\n");
        return -1;
    }

    /* Read the boot sector / BPB */
    uint8_t bpb_buf[512];
    if (read_sector(partition_lba, bpb_buf) < 0) {
        kprintf("[FAT] Failed to read boot sector at LBA %u\n", partition_lba);
        return -1;
    }

    struct fat32_bpb *bpb = (struct fat32_bpb *)bpb_buf;

    /* Validate */
    if (bpb->bytes_per_sector != 512) {
        kprintf("[FAT] Unsupported sector size: %u\n", bpb->bytes_per_sector);
        return -1;
    }
    if (bpb->num_fats == 0 || bpb->sectors_per_cluster == 0) {
        kprintf("[FAT] Invalid BPB\n");
        return -1;
    }

    /* Check if this is FAT32 (fat_size_16 == 0 means FAT32) */
    uint32_t fat_size = bpb->fat_size_16 ? bpb->fat_size_16 : bpb->fat_size_32;
    if (fat_size == 0) {
        kprintf("[FAT] Invalid FAT size\n");
        return -1;
    }

    mount.partition_lba = partition_lba;
    mount.bytes_per_sector = bpb->bytes_per_sector;
    mount.sectors_per_cluster = bpb->sectors_per_cluster;
    mount.fat_start_lba = partition_lba + bpb->reserved_sectors;
    mount.fat_size_sectors = fat_size;
    mount.data_start_lba = mount.fat_start_lba + (bpb->num_fats * fat_size);
    mount.root_cluster = bpb->root_cluster;

    uint32_t total_sectors = bpb->total_sectors_32 ? bpb->total_sectors_32 : bpb->total_sectors_16;
    uint32_t data_sectors = total_sectors - (bpb->reserved_sectors + bpb->num_fats * fat_size);
    mount.total_clusters = data_sectors / bpb->sectors_per_cluster;

    mount.mounted = true;

    kprintf("[FAT] FAT32 mounted at LBA %u\n", partition_lba);
    kprintf("[FAT]   Cluster size: %u bytes\n", mount.sectors_per_cluster * 512);
    kprintf("[FAT]   Root cluster: %u\n", mount.root_cluster);
    kprintf("[FAT]   Total clusters: %u\n", mount.total_clusters);

    return 0;
}

void fat_unmount(void) {
    /* Close all open files */
    for (int i = 0; i < FAT_MAX_FILES; i++) {
        if (file_table[i].used)
            fat_close(i);
    }
    bcache_sync();
    mount.mounted = false;
    kprintf("[FAT] Unmounted\n");
}

bool fat_is_mounted(void) {
    return mount.mounted;
}

int fat_open(const char *path, int flags) {
    if (!mount.mounted) return -1;

    struct fat_dirent de;
    uint32_t parent_cluster;
    int found = fat_resolve_path(path, &de, &parent_cluster);

    if (found < 0 && (flags & 0x01)) {
        /* Create new file */
        return fat_create(path);
    }
    if (found < 0) return -1;

    int fd = fat_alloc_fd();
    if (fd < 0) return -1;

    uint32_t cluster = ((uint32_t)de.cluster_hi << 16) | de.cluster_lo;
    file_table[fd].start_cluster = cluster;
    file_table[fd].current_cluster = cluster;
    file_table[fd].file_size = de.file_size;
    file_table[fd].offset = 0;
    file_table[fd].is_dir = (de.attr & FAT_ATTR_DIRECTORY) != 0;
    file_table[fd].dir_cluster = parent_cluster;
    file_table[fd].dirty = false;

    /* Find entry index in parent for later updates */
    if (path[1] != '\0') {
        char parent_path[256], basename[256];
        fat_split_path(path, parent_path, basename);
        char name83[11];
        fat_name_to_83(basename, name83);
        uint32_t entry_idx;
        if (fat_find_in_dir(parent_cluster, name83, NULL, &entry_idx) == 0)
            file_table[fd].dir_entry_index = entry_idx;
    }

    /* O_TRUNC: truncate file to zero length */
    if ((flags & 0x02) && !file_table[fd].is_dir) {
        fat_truncate(fd);
    }

    return fd;
}

int fat_close(int fd) {
    if (fd < 0 || fd >= FAT_MAX_FILES || !file_table[fd].used) return -1;

    /* If file was written, update directory entry with new size */
    if (file_table[fd].dirty && !file_table[fd].is_dir) {
        struct fat_dirent de;
        memset(&de, 0, sizeof(de));

        /* We need to re-read the dir entry and update size */
        uint32_t cluster_bytes = mount.sectors_per_cluster * 512;
        uint32_t entries_per_cluster = cluster_bytes / sizeof(struct fat_dirent);
        uint8_t *cbuf = kmalloc(cluster_bytes);
        if (cbuf) {
            uint32_t c = file_table[fd].dir_cluster;
            uint32_t remaining = file_table[fd].dir_entry_index;
            while (c >= 2 && c < FAT32_EOC) {
                if (remaining < entries_per_cluster) {
                    if (read_cluster(c, cbuf) == 0) {
                        struct fat_dirent *entries = (struct fat_dirent *)cbuf;
                        entries[remaining].file_size = file_table[fd].file_size;
                        write_cluster(c, cbuf);
                    }
                    break;
                }
                remaining -= entries_per_cluster;
                c = fat_read_entry(c);
            }
            kfree(cbuf);
        }
    }

    file_table[fd].used = false;
    return 0;
}

int fat_read(int fd, void *buf, size_t count) {
    if (fd < 0 || fd >= FAT_MAX_FILES || !file_table[fd].used) return -1;
    if (file_table[fd].is_dir) return -1;

    struct fat_file *f = &file_table[fd];
    uint32_t cluster_bytes = mount.sectors_per_cluster * 512;

    /* Clamp to remaining file size */
    uint32_t remaining = f->file_size - f->offset;
    if (count > remaining) count = remaining;
    if (count == 0) return 0;

    uint8_t *cbuf = kmalloc(cluster_bytes);
    if (!cbuf) return -1;

    uint8_t *dest = (uint8_t *)buf;
    size_t total_read = 0;

    while (total_read < count) {
        if (f->current_cluster < 2 || f->current_cluster >= FAT32_EOC) break;

        /* Read current cluster */
        if (read_cluster(f->current_cluster, cbuf) < 0) break;

        uint32_t offset_in_cluster = f->offset % cluster_bytes;
        uint32_t available = cluster_bytes - offset_in_cluster;
        uint32_t to_copy = count - total_read;
        if (to_copy > available) to_copy = available;

        memcpy(dest + total_read, cbuf + offset_in_cluster, to_copy);
        total_read += to_copy;
        f->offset += to_copy;

        /* Move to next cluster if we've consumed this one */
        if (f->offset % cluster_bytes == 0) {
            f->current_cluster = fat_read_entry(f->current_cluster);
        }
    }

    kfree(cbuf);
    return (int)total_read;
}

int fat_write(int fd, const void *buf, size_t count) {
    if (fd < 0 || fd >= FAT_MAX_FILES || !file_table[fd].used) return -1;
    if (file_table[fd].is_dir) return -1;

    struct fat_file *f = &file_table[fd];
    uint32_t cluster_bytes = mount.sectors_per_cluster * 512;

    if (count == 0) return 0;

    /* If file has no cluster yet, allocate one */
    if (f->start_cluster < 2) {
        uint32_t c = fat_alloc_cluster();
        if (c == 0) return -1;
        f->start_cluster = c;
        f->current_cluster = c;

        /* Update dir entry with new cluster */
        uint32_t cb = mount.sectors_per_cluster * 512;
        uint32_t epc = cb / sizeof(struct fat_dirent);
        uint8_t *cbuf2 = kmalloc(cb);
        if (cbuf2) {
            uint32_t dc = f->dir_cluster;
            uint32_t rem = f->dir_entry_index;
            while (dc >= 2 && dc < FAT32_EOC) {
                if (rem < epc) {
                    if (read_cluster(dc, cbuf2) == 0) {
                        struct fat_dirent *entries = (struct fat_dirent *)cbuf2;
                        entries[rem].cluster_lo = c & 0xFFFF;
                        entries[rem].cluster_hi = (c >> 16) & 0xFFFF;
                        write_cluster(dc, cbuf2);
                    }
                    break;
                }
                rem -= epc;
                dc = fat_read_entry(dc);
            }
            kfree(cbuf2);
        }
    }

    uint8_t *cbuf = kmalloc(cluster_bytes);
    if (!cbuf) return -1;

    const uint8_t *src = (const uint8_t *)buf;
    size_t total_written = 0;

    while (total_written < count) {
        if (f->current_cluster < 2 || f->current_cluster >= FAT32_EOC) {
            /* Need a new cluster — find the last one and extend */
            uint32_t last = f->start_cluster;
            uint32_t next = fat_read_entry(last);
            while (next >= 2 && next < FAT32_EOC) {
                last = next;
                next = fat_read_entry(last);
            }
            uint32_t nc = fat_extend_chain(last);
            if (nc == 0) break;
            f->current_cluster = nc;
        }

        /* Read-modify-write current cluster */
        if (read_cluster(f->current_cluster, cbuf) < 0) break;

        uint32_t offset_in_cluster = f->offset % cluster_bytes;
        uint32_t available = cluster_bytes - offset_in_cluster;
        uint32_t to_copy = count - total_written;
        if (to_copy > available) to_copy = available;

        memcpy(cbuf + offset_in_cluster, src + total_written, to_copy);
        if (write_cluster(f->current_cluster, cbuf) < 0) break;

        total_written += to_copy;
        f->offset += to_copy;
        if (f->offset > f->file_size)
            f->file_size = f->offset;

        if (f->offset % cluster_bytes == 0) {
            f->current_cluster = fat_read_entry(f->current_cluster);
        }
    }

    f->dirty = true;
    kfree(cbuf);
    return (int)total_written;
}

int fat_stat(const char *path, uint32_t *size, uint8_t *is_dir) {
    if (!mount.mounted) return -1;

    struct fat_dirent de;
    if (fat_resolve_path(path, &de, NULL) < 0) return -1;

    if (size) *size = de.file_size;
    if (is_dir) *is_dir = (de.attr & FAT_ATTR_DIRECTORY) ? 1 : 0;
    return 0;
}

int fat_readdir(int fd, char *name, uint32_t *size, uint8_t *is_dir) {
    if (fd < 0 || fd >= FAT_MAX_FILES || !file_table[fd].used) return -1;
    if (!file_table[fd].is_dir) return -1;

    struct fat_file *f = &file_table[fd];

    struct fat_dirent *buf;
    int count = fat_read_dir(f->start_cluster, &buf);
    if (count < 0) return -1;

    /* f->offset is used as entry index for directory iteration */
    while ((int)f->offset < count) {
        struct fat_dirent *de = &buf[f->offset];
        f->offset++;

        if ((uint8_t)de->name[0] == 0x00) { kfree(buf); return -1; } /* End */
        if ((uint8_t)de->name[0] == 0xE5) continue; /* Deleted */
        if (de->attr == FAT_ATTR_LFN) continue;
        if (de->attr & FAT_ATTR_VOLUME_ID) continue;

        /* Skip . and .. */
        if (de->name[0] == '.' && (de->name[1] == ' ' || de->name[1] == '.'))
            continue;

        if (name) fat_83_to_name(de, name);
        if (size) *size = de->file_size;
        if (is_dir) *is_dir = (de->attr & FAT_ATTR_DIRECTORY) ? 1 : 0;

        kfree(buf);
        return 0;
    }

    kfree(buf);
    return -1; /* No more entries */
}

int fat_mkdir(const char *path) {
    if (!mount.mounted) return -1;

    char parent_path[256], basename[256];
    if (fat_split_path(path, parent_path, basename) < 0) return -1;

    /* Resolve parent */
    struct fat_dirent parent_de;
    if (fat_resolve_path(parent_path, &parent_de, NULL) < 0) return -1;
    if (!(parent_de.attr & FAT_ATTR_DIRECTORY)) return -1;

    uint32_t parent_cluster = ((uint32_t)parent_de.cluster_hi << 16) | parent_de.cluster_lo;

    /* Check if already exists */
    struct fat_dirent dummy;
    if (fat_resolve_path(path, &dummy, NULL) == 0) return -1;

    /* Allocate cluster for new directory */
    uint32_t new_cluster = fat_alloc_cluster();
    if (new_cluster == 0) return -1;

    /* Create . and .. entries */
    uint32_t cluster_bytes = mount.sectors_per_cluster * 512;
    uint8_t *cbuf = kmalloc(cluster_bytes);
    if (!cbuf) return -1;
    memset(cbuf, 0, cluster_bytes);

    struct fat_dirent *entries = (struct fat_dirent *)cbuf;

    /* . entry */
    memset(entries[0].name, ' ', 8);
    memset(entries[0].ext, ' ', 3);
    entries[0].name[0] = '.';
    entries[0].attr = FAT_ATTR_DIRECTORY;
    entries[0].cluster_lo = new_cluster & 0xFFFF;
    entries[0].cluster_hi = (new_cluster >> 16) & 0xFFFF;

    /* .. entry */
    memset(entries[1].name, ' ', 8);
    memset(entries[1].ext, ' ', 3);
    entries[1].name[0] = '.';
    entries[1].name[1] = '.';
    entries[1].attr = FAT_ATTR_DIRECTORY;
    entries[1].cluster_lo = parent_cluster & 0xFFFF;
    entries[1].cluster_hi = (parent_cluster >> 16) & 0xFFFF;

    write_cluster(new_cluster, cbuf);
    kfree(cbuf);

    /* Add entry in parent directory */
    struct fat_dirent new_entry;
    memset(&new_entry, 0, sizeof(new_entry));
    char name83[11];
    fat_name_to_83(basename, name83);
    memcpy(new_entry.name, name83, 8);
    memcpy(new_entry.ext, name83 + 8, 3);
    new_entry.attr = FAT_ATTR_DIRECTORY;
    new_entry.cluster_lo = new_cluster & 0xFFFF;
    new_entry.cluster_hi = (new_cluster >> 16) & 0xFFFF;

    return fat_add_dir_entry(parent_cluster, &new_entry);
}

int fat_create(const char *path) {
    if (!mount.mounted) return -1;

    char parent_path[256], basename[256];
    if (fat_split_path(path, parent_path, basename) < 0) return -1;

    struct fat_dirent parent_de;
    if (fat_resolve_path(parent_path, &parent_de, NULL) < 0) return -1;
    if (!(parent_de.attr & FAT_ATTR_DIRECTORY)) return -1;

    uint32_t parent_cluster = ((uint32_t)parent_de.cluster_hi << 16) | parent_de.cluster_lo;

    /* Check if already exists */
    struct fat_dirent dummy;
    if (fat_resolve_path(path, &dummy, NULL) == 0) {
        /* File exists, just open it */
        return fat_open(path, 0);
    }

    /* Add entry in parent directory (no cluster yet — will be allocated on first write) */
    struct fat_dirent new_entry;
    memset(&new_entry, 0, sizeof(new_entry));
    char name83[11];
    fat_name_to_83(basename, name83);
    memcpy(new_entry.name, name83, 8);
    memcpy(new_entry.ext, name83 + 8, 3);
    new_entry.attr = FAT_ATTR_ARCHIVE;

    if (fat_add_dir_entry(parent_cluster, &new_entry) < 0) return -1;

    /* Now open the file */
    return fat_open(path, 0);
}

/* ---- Free a cluster chain ---- */

static void fat_free_chain(uint32_t cluster) {
    while (cluster >= 2 && cluster < FAT32_EOC) {
        uint32_t next = fat_read_entry(cluster);
        fat_write_entry(cluster, FAT32_FREE);
        cluster = next;
    }
}

/* ---- Unlink (delete) a file ---- */

int fat_unlink(const char *path) {
    if (!mount.mounted) return -1;

    /* Resolve the file */
    struct fat_dirent de;
    uint32_t parent_cluster;
    if (fat_resolve_path(path, &de, &parent_cluster) < 0) return -1;

    /* Don't delete directories (use rmdir for that) */
    if (de.attr & FAT_ATTR_DIRECTORY) return -1;

    /* Find the entry index in the parent directory */
    char parent_path[256], basename[256];
    if (fat_split_path(path, parent_path, basename) < 0) return -1;

    char name83[11];
    fat_name_to_83(basename, name83);

    uint32_t entry_index;
    if (fat_find_in_dir(parent_cluster, name83, NULL, &entry_index) < 0) return -1;

    /* Free the cluster chain */
    uint32_t start_cluster = ((uint32_t)de.cluster_hi << 16) | de.cluster_lo;
    if (start_cluster >= 2)
        fat_free_chain(start_cluster);

    /* Mark directory entry as deleted (0xE5) */
    uint32_t cluster_bytes = mount.sectors_per_cluster * 512;
    uint32_t entries_per_cluster = cluster_bytes / sizeof(struct fat_dirent);
    uint8_t *cbuf = kmalloc(cluster_bytes);
    if (!cbuf) return -1;

    uint32_t c = parent_cluster;
    uint32_t remaining = entry_index;
    while (c >= 2 && c < FAT32_EOC) {
        if (remaining < entries_per_cluster) {
            if (read_cluster(c, cbuf) == 0) {
                struct fat_dirent *entries = (struct fat_dirent *)cbuf;
                entries[remaining].name[0] = (char)0xE5;
                write_cluster(c, cbuf);
            }
            break;
        }
        remaining -= entries_per_cluster;
        c = fat_read_entry(c);
    }

    kfree(cbuf);
    return 0;
}

/* ---- Truncate an open file to zero length ---- */

int fat_truncate(int fd) {
    if (fd < 0 || fd >= FAT_MAX_FILES || !file_table[fd].used) return -1;
    if (file_table[fd].is_dir) return -1;

    struct fat_file *f = &file_table[fd];

    /* Free cluster chain */
    if (f->start_cluster >= 2)
        fat_free_chain(f->start_cluster);

    /* Reset file state */
    f->start_cluster = 0;
    f->current_cluster = 0;
    f->file_size = 0;
    f->offset = 0;
    f->dirty = true;

    /* Update dir entry: zero out cluster and size */
    uint32_t cluster_bytes = mount.sectors_per_cluster * 512;
    uint32_t entries_per_cluster = cluster_bytes / sizeof(struct fat_dirent);
    uint8_t *cbuf = kmalloc(cluster_bytes);
    if (cbuf) {
        uint32_t c = f->dir_cluster;
        uint32_t remaining = f->dir_entry_index;
        while (c >= 2 && c < FAT32_EOC) {
            if (remaining < entries_per_cluster) {
                if (read_cluster(c, cbuf) == 0) {
                    struct fat_dirent *entries = (struct fat_dirent *)cbuf;
                    entries[remaining].file_size = 0;
                    entries[remaining].cluster_lo = 0;
                    entries[remaining].cluster_hi = 0;
                    write_cluster(c, cbuf);
                }
                break;
            }
            remaining -= entries_per_cluster;
            c = fat_read_entry(c);
        }
        kfree(cbuf);
    }

    return 0;
}

/* ---- MBR partition table parsing ---- */

struct mbr_partition {
    uint8_t  status;
    uint8_t  chs_first[3];
    uint8_t  type;
    uint8_t  chs_last[3];
    uint32_t lba_start;
    uint32_t sector_count;
} PACKED;

int fat_mount_first_partition(void) {
    uint8_t mbr[512];
    if (read_sector(0, mbr) < 0) {
        kprintf("[FAT] Failed to read MBR\n");
        return -1;
    }

    /* Check MBR signature */
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        /* No MBR — try treating entire disk as FAT32 (no partition table) */
        kprintf("[FAT] No MBR signature, trying raw FAT32 at LBA 0\n");
        return fat_mount(0);
    }

    struct mbr_partition *ptable = (struct mbr_partition *)(mbr + 446);

    for (int i = 0; i < 4; i++) {
        uint8_t type = ptable[i].type;
        /* FAT32 partition types: 0x0B (FAT32), 0x0C (FAT32 LBA) */
        if (type == 0x0B || type == 0x0C || type == 0x0E || type == 0x06) {
            kprintf("[FAT] Found partition %d: type=0x%02x LBA=%u size=%u sectors\n",
                    i, type, ptable[i].lba_start, ptable[i].sector_count);
            return fat_mount(ptable[i].lba_start);
        }
    }

    /* No FAT partition found — try raw */
    kprintf("[FAT] No FAT partition in MBR, trying LBA 0\n");
    return fat_mount(0);
}
