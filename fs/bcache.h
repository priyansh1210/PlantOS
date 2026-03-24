#ifndef FS_BCACHE_H
#define FS_BCACHE_H

#include <plantos/types.h>

#define BCACHE_SIZE      64    /* Number of cached sectors */
#define SECTOR_SIZE      512

struct bcache_entry {
    uint32_t lba;
    uint8_t  data[SECTOR_SIZE];
    bool     valid;
    bool     dirty;
    uint32_t access_tick;     /* For LRU eviction */
};

void bcache_init(void);

/* Read a sector — returns pointer to cached data (do NOT free) */
uint8_t *bcache_read(uint32_t lba);

/* Mark a cached sector as dirty (must have been read first) */
void bcache_mark_dirty(uint32_t lba);

/* Write data to a sector through the cache */
int bcache_write(uint32_t lba, const void *data);

/* Flush all dirty buffers to disk */
int bcache_sync(void);

/* Invalidate entire cache (e.g., on unmount) */
void bcache_invalidate(void);

/* Stats */
uint32_t bcache_get_hits(void);
uint32_t bcache_get_misses(void);

#endif
