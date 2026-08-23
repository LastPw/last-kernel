#include "page_cache.h"
#include "string.h"
#include "spinlock.h"
#include "serial.h"

static page_cache_entry_t g_entries[PAGE_CACHE_MAX_PAGES];
static page_cache_entry_t* g_hash_table[PAGE_CACHE_HASH_BUCKETS];
static page_cache_entry_t* g_lru_head = NULL;
static page_cache_entry_t* g_lru_tail = NULL;
static spinlock_t g_cache_lock;
static page_cache_stats_t g_stats;
static u32 g_last_lba = 0xFFFFFFFFu;
static u8 g_initialized = 0u;

static inline u32 cache_hash(u32 dev_id, u32 lba) {
    return ((dev_id * 31u) ^ (lba * 2654435761u)) & (PAGE_CACHE_HASH_BUCKETS - 1u);
}

static void lru_remove(page_cache_entry_t* e) {
    if (!e) return;
    if (e->lru_prev) {
        e->lru_prev->lru_next = e->lru_next;
    } else {
        g_lru_head = e->lru_next;
    }
    if (e->lru_next) {
        e->lru_next->lru_prev = e->lru_prev;
    } else {
        g_lru_tail = e->lru_prev;
    }
    e->lru_prev = NULL;
    e->lru_next = NULL;
}

static void lru_insert_head(page_cache_entry_t* e) {
    if (!e) return;
    e->lru_next = g_lru_head;
    e->lru_prev = NULL;
    if (g_lru_head) {
        g_lru_head->lru_prev = e;
    }
    g_lru_head = e;
    if (!g_lru_tail) {
        g_lru_tail = e;
    }
}

static void hash_insert(page_cache_entry_t* e) {
    u32 bucket = cache_hash(e->dev_id, e->lba);
    e->hash_next = g_hash_table[bucket];
    g_hash_table[bucket] = e;
}

static void hash_remove(page_cache_entry_t* e) {
    u32 bucket = cache_hash(e->dev_id, e->lba);
    page_cache_entry_t** curr = &g_hash_table[bucket];
    while (*curr) {
        if (*curr == e) {
            *curr = e->hash_next;
            e->hash_next = NULL;
            return;
        }
        curr = &((*curr)->hash_next);
    }
}

void page_cache_init(void) {
    if (g_initialized) return;
    spinlock_init(&g_cache_lock);
    memset(g_entries, 0, sizeof(g_entries));
    memset(g_hash_table, 0, sizeof(g_hash_table));
    g_lru_head = NULL;
    g_lru_tail = NULL;
    memset(&g_stats, 0, sizeof(g_stats));
    g_initialized = 1u;
    serial_writeln("[PAGE_CACHE] Unified Hash-LRU Page Cache (4096 Pages, 2MB Cache Pool) initialized");
}

static page_cache_entry_t* cache_lookup(u32 dev_id, u32 lba) {
    u32 bucket = cache_hash(dev_id, lba);
    page_cache_entry_t* curr = g_hash_table[bucket];
    while (curr) {
        if (curr->valid && curr->dev_id == dev_id && curr->lba == lba) {
            return curr;
        }
        curr = curr->hash_next;
    }
    return NULL;
}

static page_cache_entry_t* cache_allocate_slot(int (*raw_write)(u32 dev_id, u32 lba, const u8* in512)) {
    /* 1. First search for an unallocated entry */
    for (u32 i = 0; i < PAGE_CACHE_MAX_PAGES; i++) {
        if (!g_entries[i].valid) {
            return &g_entries[i];
        }
    }

    /* 2. If pool is full, evict the LRU tail */
    page_cache_entry_t* victim = g_lru_tail;
    if (!victim) return NULL;

    /* Flush dirty buffer before eviction */
    if (victim->valid && victim->dirty && raw_write) {
        raw_write(victim->dev_id, victim->lba, victim->data);
        victim->dirty = 0u;
        g_stats.dirty_flushes++;
    }

    hash_remove(victim);
    lru_remove(victim);
    return victim;
}

int page_cache_read_sector(u32 dev_id, u32 lba, u8* out512, int (*raw_read)(u32 dev_id, u32 lba, u8* out512)) {
    if (!out512 || !raw_read) return -1;
    page_cache_init();

    spinlock_lock(&g_cache_lock);
    g_stats.total_lookups++;

    page_cache_entry_t* hit = cache_lookup(dev_id, lba);
    if (hit) {
        g_stats.cache_hits++;
        memcpy(out512, hit->data, PAGE_CACHE_SECTOR_SIZE);
        lru_remove(hit);
        lru_insert_head(hit);
        spinlock_unlock(&g_cache_lock);
        return 0;
    }

    g_stats.cache_misses++;

    /* Cache Miss: Read from underlying device */
    page_cache_entry_t* slot = cache_allocate_slot(NULL);
    if (!slot) {
        spinlock_unlock(&g_cache_lock);
        return raw_read(dev_id, lba, out512);
    }

    if (raw_read(dev_id, lba, slot->data) != 0) {
        spinlock_unlock(&g_cache_lock);
        return -1;
    }

    slot->valid = 1u;
    slot->dirty = 0u;
    slot->dev_id = dev_id;
    slot->lba = lba;
    hash_insert(slot);
    lru_insert_head(slot);

    memcpy(out512, slot->data, PAGE_CACHE_SECTOR_SIZE);

    /* Sequential Read-Ahead Detection */
    if (g_last_lba != 0xFFFFFFFFu && lba == g_last_lba + 1u) {
        g_stats.read_aheads++;
    }
    g_last_lba = lba;

    spinlock_unlock(&g_cache_lock);
    return 0;
}

int page_cache_write_sector(u32 dev_id, u32 lba, const u8* in512, int (*raw_write)(u32 dev_id, u32 lba, const u8* in512)) {
    if (!in512) return -1;
    page_cache_init();

    spinlock_lock(&g_cache_lock);
    g_stats.total_lookups++;

    page_cache_entry_t* hit = cache_lookup(dev_id, lba);
    if (!hit) {
        hit = cache_allocate_slot(raw_write);
        if (!hit) {
            spinlock_unlock(&g_cache_lock);
            return raw_write ? raw_write(dev_id, lba, in512) : -1;
        }
        hit->valid = 1u;
        hit->dev_id = dev_id;
        hit->lba = lba;
        hash_insert(hit);
    }

    memcpy(hit->data, in512, PAGE_CACHE_SECTOR_SIZE);
    hit->dirty = 1u;
    lru_remove(hit);
    lru_insert_head(hit);

    /* Write-through to ensure durability */
    if (raw_write) {
        raw_write(dev_id, lba, in512);
        hit->dirty = 0u;
        g_stats.dirty_flushes++;
    }

    spinlock_unlock(&g_cache_lock);
    return 0;
}

int page_cache_sync(int (*raw_write)(u32 dev_id, u32 lba, const u8* in512)) {
    if (!raw_write) return 0;
    spinlock_lock(&g_cache_lock);
    for (u32 i = 0; i < PAGE_CACHE_MAX_PAGES; i++) {
        if (g_entries[i].valid && g_entries[i].dirty) {
            raw_write(g_entries[i].dev_id, g_entries[i].lba, g_entries[i].data);
            g_entries[i].dirty = 0u;
            g_stats.dirty_flushes++;
        }
    }
    spinlock_unlock(&g_cache_lock);
    return 0;
}

void page_cache_get_stats(page_cache_stats_t* out) {
    if (!out) return;
    spinlock_lock(&g_cache_lock);
    memcpy(out, &g_stats, sizeof(page_cache_stats_t));
    if (g_stats.total_lookups > 0) {
        out->hit_rate_permille = (g_stats.cache_hits * 1000u) / g_stats.total_lookups;
    }
    spinlock_unlock(&g_cache_lock);
}

void page_cache_invalidate_device(u32 dev_id) {
    spinlock_lock(&g_cache_lock);
    for (u32 i = 0; i < PAGE_CACHE_MAX_PAGES; i++) {
        if (g_entries[i].valid && g_entries[i].dev_id == dev_id) {
            hash_remove(&g_entries[i]);
            lru_remove(&g_entries[i]);
            memset(&g_entries[i], 0, sizeof(page_cache_entry_t));
        }
    }
    spinlock_unlock(&g_cache_lock);
}
