#ifndef PAGE_CACHE_H
#define PAGE_CACHE_H

#include "types.h"

#define PAGE_CACHE_HASH_BUCKETS 1024u
#define PAGE_CACHE_MAX_PAGES    4096u
#define PAGE_CACHE_SECTOR_SIZE  512u
#define PAGE_CACHE_READ_AHEAD   8u

typedef struct page_cache_entry {
    u8   valid;
    u8   dirty;
    u8   reserved[2];
    u32  dev_id;
    u32  lba;
    u32  age;
    u8   data[PAGE_CACHE_SECTOR_SIZE];
    struct page_cache_entry* hash_next;
    struct page_cache_entry* lru_prev;
    struct page_cache_entry* lru_next;
} page_cache_entry_t;

typedef struct {
    u32 total_lookups;
    u32 cache_hits;
    u32 cache_misses;
    u32 dirty_flushes;
    u32 read_aheads;
    u32 hit_rate_permille;
} page_cache_stats_t;

void page_cache_init(void);
int  page_cache_read_sector(u32 dev_id, u32 lba, u8* out512, int (*raw_read)(u32 dev_id, u32 lba, u8* out512));
int  page_cache_write_sector(u32 dev_id, u32 lba, const u8* in512, int (*raw_write)(u32 dev_id, u32 lba, const u8* in512));
int  page_cache_sync(int (*raw_write)(u32 dev_id, u32 lba, const u8* in512));
void page_cache_get_stats(page_cache_stats_t* out);
void page_cache_invalidate_device(u32 dev_id);

#endif /* PAGE_CACHE_H */
