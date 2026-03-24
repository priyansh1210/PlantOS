#include "fs/bcache.h"
#include "drivers/ata.h"
#include "lib/string.h"
#include "lib/printf.h"

static struct bcache_entry cache[BCACHE_SIZE];
static uint32_t tick_counter = 0;
static uint32_t hits = 0;
static uint32_t misses = 0;

void bcache_init(void) {
    memset(cache, 0, sizeof(cache));
    tick_counter = 0;
    hits = 0;
    misses = 0;
    kprintf("[BCACHE] Buffer cache initialized (%d sectors, %d KB)\n",
            BCACHE_SIZE, (BCACHE_SIZE * SECTOR_SIZE) / 1024);
}

/* Find cache entry for a given LBA, or NULL */
static struct bcache_entry *bcache_find(uint32_t lba) {
    for (int i = 0; i < BCACHE_SIZE; i++) {
        if (cache[i].valid && cache[i].lba == lba)
            return &cache[i];
    }
    return NULL;
}

/* Find a free slot or evict the least recently used entry */
static struct bcache_entry *bcache_evict(void) {
    /* First try an empty slot */
    for (int i = 0; i < BCACHE_SIZE; i++) {
        if (!cache[i].valid)
            return &cache[i];
    }

    /* LRU eviction: find entry with lowest access_tick */
    int lru = 0;
    for (int i = 1; i < BCACHE_SIZE; i++) {
        if (cache[i].access_tick < cache[lru].access_tick)
            lru = i;
    }

    /* Flush if dirty */
    struct bcache_entry *e = &cache[lru];
    if (e->dirty) {
        ata_write_sectors(e->lba, 1, e->data);
        e->dirty = false;
    }

    e->valid = false;
    return e;
}

uint8_t *bcache_read(uint32_t lba) {
    struct bcache_entry *e = bcache_find(lba);
    if (e) {
        hits++;
        e->access_tick = ++tick_counter;
        return e->data;
    }

    /* Cache miss — read from disk */
    misses++;
    e = bcache_evict();

    if (ata_read_sectors(lba, 1, e->data) < 0)
        return NULL;

    e->lba = lba;
    e->valid = true;
    e->dirty = false;
    e->access_tick = ++tick_counter;
    return e->data;
}

void bcache_mark_dirty(uint32_t lba) {
    struct bcache_entry *e = bcache_find(lba);
    if (e) e->dirty = true;
}

int bcache_write(uint32_t lba, const void *data) {
    struct bcache_entry *e = bcache_find(lba);
    if (!e) {
        e = bcache_evict();
        e->lba = lba;
        e->valid = true;
        e->access_tick = ++tick_counter;
    }
    memcpy(e->data, data, SECTOR_SIZE);
    e->dirty = true;
    e->access_tick = ++tick_counter;
    return 0;
}

int bcache_sync(void) {
    int flushed = 0;
    for (int i = 0; i < BCACHE_SIZE; i++) {
        if (cache[i].valid && cache[i].dirty) {
            if (ata_write_sectors(cache[i].lba, 1, cache[i].data) < 0)
                return -1;
            cache[i].dirty = false;
            flushed++;
        }
    }
    return flushed;
}

void bcache_invalidate(void) {
    bcache_sync();
    memset(cache, 0, sizeof(cache));
}

uint32_t bcache_get_hits(void)   { return hits; }
uint32_t bcache_get_misses(void) { return misses; }
