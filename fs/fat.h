#ifndef FS_FAT_H
#define FS_FAT_H

#include <plantos/types.h>

/* FAT32 Boot Parameter Block (BPB) */
struct fat32_bpb {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entry_count;    /* 0 for FAT32 */
    uint16_t total_sectors_16;    /* 0 for FAT32 */
    uint8_t  media_type;
    uint16_t fat_size_16;         /* 0 for FAT32 */
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    /* FAT32 extended */
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_sig;
    uint32_t volume_serial;
    char     volume_label[11];
    char     fs_type[8];
} PACKED;

/* FAT32 directory entry */
struct fat_dirent {
    char     name[8];
    char     ext[3];
    uint8_t  attr;
    uint8_t  reserved;
    uint8_t  create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t cluster_hi;
    uint16_t modify_time;
    uint16_t modify_date;
    uint16_t cluster_lo;
    uint32_t file_size;
} PACKED;

/* Long filename entry */
struct fat_lfn_entry {
    uint8_t  order;
    uint16_t name1[5];
    uint8_t  attr;
    uint8_t  type;
    uint8_t  checksum;
    uint16_t name2[6];
    uint16_t first_cluster;
    uint16_t name3[2];
} PACKED;

/* Directory entry attributes */
#define FAT_ATTR_READONLY   0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUME_ID  0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20
#define FAT_ATTR_LFN        0x0F

/* Cluster special values */
#define FAT32_EOC       0x0FFFFFF8
#define FAT32_FREE      0x00000000
#define FAT32_BAD       0x0FFFFFF7

/* Mount info */
struct fat_mount {
    uint32_t partition_lba;       /* LBA offset of partition start */
    uint32_t fat_start_lba;       /* LBA of first FAT */
    uint32_t data_start_lba;      /* LBA of data region (cluster 2) */
    uint32_t root_cluster;
    uint32_t sectors_per_cluster;
    uint32_t fat_size_sectors;
    uint32_t bytes_per_sector;
    uint32_t total_clusters;
    bool     mounted;
};

/* FAT file handle (for VFS integration) */
#define FAT_MAX_FILES 16

struct fat_file {
    bool     used;
    uint32_t start_cluster;
    uint32_t current_cluster;
    uint32_t file_size;
    uint32_t offset;
    uint32_t dir_cluster;         /* Parent directory cluster */
    uint32_t dir_entry_index;     /* Index of dirent in parent */
    bool     is_dir;
    bool     dirty;
};

void fat_init(void);
int  fat_mount(uint32_t partition_lba);
void fat_unmount(void);
bool fat_is_mounted(void);

/* File operations */
int  fat_open(const char *path, int flags);
int  fat_close(int fd);
int  fat_read(int fd, void *buf, size_t count);
int  fat_write(int fd, const void *buf, size_t count);
int  fat_stat(const char *path, uint32_t *size, uint8_t *is_dir);
int  fat_readdir(int fd, char *name, uint32_t *size, uint8_t *is_dir);
int  fat_mkdir(const char *path);
int  fat_create(const char *path);

/* Delete a file */
int  fat_unlink(const char *path);

/* Truncate an open file to zero length */
int  fat_truncate(int fd);

/* Auto-detect partition and mount */
int  fat_mount_first_partition(void);

#endif
