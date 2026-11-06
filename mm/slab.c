#include "slab.h"
#include "kmalloc.h"
#include "frame_allocator.h"
#include "paging.h"
#include "string.h"
#include "serial.h"
#include "rust_bridge.h"

#define SLAB_PAGE_SIZE      4096u
#define SLAB_MAGIC          0x534C4142u /* "SLAB" */
#define BIG_ALLOC_MAGIC     0x42494741u /* "BIGA" */
#define MAX_KMEM_CACHES     32u
#define NUM_GENERAL_CACHES  8u

typedef struct slab_obj {
    struct slab_obj* next;
} slab_obj_t;

typedef struct kmem_slab kmem_slab_t;

struct kmem_slab {
    u32           magic;
    kmem_cache_t* cache;
    u16           total_objs;
    u16           free_objs;
    slab_obj_t*   freelist;
    kmem_slab_t*  next;
    kmem_slab_t*  prev;
};

struct kmem_cache {
    u8           used;
    char         name[KMEM_CACHE_NAME_LEN];
    u32          obj_size;
    u32          align;
    u32          flags;
    u32          objs_per_slab;
    u32          total_objs;
    u32          active_objs;
    kmem_slab_t* slabs_partial;
    kmem_slab_t* slabs_full;
    kmem_slab_t* slabs_free;
};

typedef struct {
    u32 magic;
    u32 order;
    u64 size;
    u64 padding;
} big_alloc_hdr_t;

static kmem_cache_t s_caches[MAX_KMEM_CACHES];
static const u32 s_general_sizes[NUM_GENERAL_CACHES] = {
    16u, 32u, 64u, 128u, 256u, 512u, 1024u, 2048u
};
static kmem_cache_t* s_general_caches[NUM_GENERAL_CACHES];

kmem_cache_t* g_task_struct_cache = 0;
kmem_cache_t* g_file_cache = 0;

static u32 align_up(u32 val, u32 align) {
    if (align == 0) return val;
    return (val + align - 1) & ~(align - 1);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Slab Allocator Implementation
 * ═══════════════════════════════════════════════════════════════════════ */

kmem_cache_t* kmem_cache_create(const char* name, size_t size, size_t align, u32 flags) {
    if (size == 0) return 0;
    if (align < 8) align = 8;

    for (u32 i = 0; i < MAX_KMEM_CACHES; i++) {
        if (!s_caches[i].used) {
            kmem_cache_t* c = &s_caches[i];
            memset(c, 0, sizeof(*c));
            c->used = 1;
            if (name) {
                str_lcpy(c->name, name, KMEM_CACHE_NAME_LEN);
            } else {
                str_lcpy(c->name, "unnamed_cache", KMEM_CACHE_NAME_LEN);
            }

            u32 aligned_obj = align_up((u32)size, (u32)align);
            if (aligned_obj < sizeof(slab_obj_t)) {
                aligned_obj = sizeof(slab_obj_t);
            }
            c->obj_size = aligned_obj;
            c->align = (u32)align;
            c->flags = flags;

            u32 hdr_size = align_up(sizeof(kmem_slab_t), (u32)align);
            c->objs_per_slab = (SLAB_PAGE_SIZE > hdr_size) ? ((SLAB_PAGE_SIZE - hdr_size) / aligned_obj) : 0;
            return c;
        }
    }
    return 0;
}

static kmem_slab_t* slab_grow(kmem_cache_t* cache) {
    void* pphys = frame_alloc();
    if (!pphys) return 0;

    void* page = (void*)paging_phys_to_virt((u64)(uintptr_t)pphys);
    memset(page, 0, SLAB_PAGE_SIZE);

    kmem_slab_t* slab = (kmem_slab_t*)page;
    slab->magic = SLAB_MAGIC;
    slab->cache = cache;
    slab->total_objs = (u16)cache->objs_per_slab;
    slab->free_objs = (u16)cache->objs_per_slab;

    u32 hdr_size = align_up(sizeof(kmem_slab_t), cache->align);
    u8* obj_base = (u8*)page + hdr_size;

    slab->freelist = 0;
    for (u32 i = 0; i < cache->objs_per_slab; i++) {
        slab_obj_t* obj = (slab_obj_t*)(obj_base + (i * cache->obj_size));
        obj->next = slab->freelist;
        slab->freelist = obj;
    }

    /* Link into slabs_free list */
    slab->next = cache->slabs_free;
    slab->prev = 0;
    if (cache->slabs_free) {
        cache->slabs_free->prev = slab;
    }
    cache->slabs_free = slab;
    cache->total_objs += cache->objs_per_slab;
    return slab;
}

void* kmem_cache_alloc(kmem_cache_t* cache) {
    if (!cache || !cache->used || cache->objs_per_slab == 0) return 0;

    kmem_slab_t* slab = cache->slabs_partial;
    if (!slab) {
        slab = cache->slabs_free;
    }
    if (!slab) {
        slab = slab_grow(cache);
        if (!slab) return 0;
    }

    /* Remove object from slab freelist */
    slab_obj_t* obj = slab->freelist;
    if (!obj) return 0;
    slab->freelist = obj->next;
    slab->free_objs--;
    cache->active_objs++;

    /* Move slab between lists if needed */
    if (slab->free_objs == 0) {
        /* Move from partial or free to full */
        if (slab->prev) slab->prev->next = slab->next;
        if (slab->next) slab->next->prev = slab->prev;
        if (cache->slabs_partial == slab) cache->slabs_partial = slab->next;
        if (cache->slabs_free == slab) cache->slabs_free = slab->next;

        slab->next = cache->slabs_full;
        slab->prev = 0;
        if (cache->slabs_full) cache->slabs_full->prev = slab;
        cache->slabs_full = slab;
    } else if (slab->free_objs == slab->total_objs - 1 && cache->slabs_free == slab) {
        /* Move from free to partial */
        cache->slabs_free = slab->next;
        if (slab->next) slab->next->prev = 0;

        slab->next = cache->slabs_partial;
        slab->prev = 0;
        if (cache->slabs_partial) cache->slabs_partial->prev = slab;
        cache->slabs_partial = slab;
    }

    return (void*)obj;
}

void kmem_cache_free(kmem_cache_t* cache, void* obj) {
    if (!cache || !obj) return;

    /* Slab header is at the beginning of the 4KB page */
    u64 page_addr = (u64)(uintptr_t)obj & ~(u64)(SLAB_PAGE_SIZE - 1);
    kmem_slab_t* slab = (kmem_slab_t*)(uintptr_t)page_addr;
    if (slab->magic != SLAB_MAGIC) {
        serial_writeln("[Slab] ERROR: Invalid slab magic in kmem_cache_free!");
        return;
    }

    slab_obj_t* slab_obj = (slab_obj_t*)obj;
    slab_obj->next = slab->freelist;
    slab->freelist = slab_obj;
    slab->free_objs++;
    if (cache->active_objs > 0) cache->active_objs--;

    /* Update slab position among lists */
    if (slab->free_objs == slab->total_objs) {
        /* Move to free list */
        if (slab->prev) slab->prev->next = slab->next;
        if (slab->next) slab->next->prev = slab->prev;
        if (cache->slabs_partial == slab) cache->slabs_partial = slab->next;
        if (cache->slabs_full == slab) cache->slabs_full = slab->next;

        slab->next = cache->slabs_free;
        slab->prev = 0;
        if (cache->slabs_free) cache->slabs_free->prev = slab;
        cache->slabs_free = slab;
    }
}

int kmem_cache_destroy(kmem_cache_t* cache) {
    if (!cache || !cache->used) return -1;
    /* Free all pages in free list */
    kmem_slab_t* s = cache->slabs_free;
    while (s) {
        kmem_slab_t* next = s->next;
        u64 phys = paging_virt_to_phys((u64)(uintptr_t)s);
        frame_free((void*)(uintptr_t)phys);
        s = next;
    }
    cache->used = 0;
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  General Kernel Allocator (kmalloc, kzalloc, kfree)
 * ═══════════════════════════════════════════════════════════════════════ */

void slab_init(void) {
    memset(s_caches, 0, sizeof(s_caches));

    /* Create general-purpose size caches */
    char name_buf[KMEM_CACHE_NAME_LEN];
    for (u32 i = 0; i < NUM_GENERAL_CACHES; i++) {
        str_lcpy(name_buf, "kmalloc-", KMEM_CACHE_NAME_LEN);
        /* Format size string */
        u32 sz = s_general_sizes[i];
        if (sz >= 1000) {
            str_lcat(name_buf, (sz == 1024) ? "1024" : "2048", KMEM_CACHE_NAME_LEN);
        } else if (sz >= 100) {
            char num[4] = { (char)('0' + (sz/100)), (char)('0' + ((sz/10)%10)), (char)('0' + (sz%10)), '\0' };
            str_lcat(name_buf, num, KMEM_CACHE_NAME_LEN);
        } else {
            char num[3] = { (char)('0' + (sz/10)), (char)('0' + (sz%10)), '\0' };
            str_lcat(name_buf, num, KMEM_CACHE_NAME_LEN);
        }
        s_general_caches[i] = kmem_cache_create(name_buf, sz, 8, 0);
    }

    /* Create dedicated caches */
    g_task_struct_cache = kmem_cache_create("task_struct", 3008, 64, 0);
    g_file_cache = kmem_cache_create("kfile", 128, 8, 0);

    serial_writeln("[Slab] Monolithic Buddy & Slab allocator initialized with dedicated caches");
}

void* kmalloc(size_t size) {
    if (size == 0) return 0;

    void* res = 0;
    /* Check if fits in general slab caches */
    for (u32 i = 0; i < NUM_GENERAL_CACHES; i++) {
        if (size <= s_general_sizes[i]) {
            res = kmem_cache_alloc(s_general_caches[i]);
            if (res) rust_bridge_mem_on_alloc((u64)size);
            return res;
        }
    }

    /* Big Allocation: allocate contiguous physical pages via Buddy Allocator */
    u64 total_bytes = sizeof(big_alloc_hdr_t) + size;
    u32 pages_needed = (u32)((total_bytes + SLAB_PAGE_SIZE - 1) / SLAB_PAGE_SIZE);

    u8 order = 0;
    while ((1u << order) < pages_needed && order < 10) {
        order++;
    }

    void* pphys = frame_alloc_order(order);
    if (!pphys) return 0;

    void* pvirt = (void*)paging_phys_to_virt((u64)(uintptr_t)pphys);
    big_alloc_hdr_t* hdr = (big_alloc_hdr_t*)pvirt;
    hdr->magic = BIG_ALLOC_MAGIC;
    hdr->order = order;
    hdr->size = size;

    rust_bridge_mem_on_alloc((u64)size);
    return (void*)((u8*)pvirt + sizeof(big_alloc_hdr_t));
}

void* kzalloc(size_t size) {
    void* ptr = kmalloc(size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

void kfree(void* ptr) {
    if (!ptr) return;

    /* Check if this is a Big Allocation */
    u64 addr = (u64)(uintptr_t)ptr;
    if ((addr & (SLAB_PAGE_SIZE - 1)) == sizeof(big_alloc_hdr_t)) {
        big_alloc_hdr_t* hdr = (big_alloc_hdr_t*)(uintptr_t)(addr - sizeof(big_alloc_hdr_t));
        if (hdr->magic == BIG_ALLOC_MAGIC) {
            u8 order = (u8)hdr->order;
            u64 sz = hdr->size;
            u64 phys = paging_virt_to_phys((u64)(uintptr_t)hdr);
            frame_free_order((void*)(uintptr_t)phys, order);
            rust_bridge_mem_on_free(sz);
            return;
        }
    }

    /* Otherwise find the slab page header */
    u64 page_addr = addr & ~(u64)(SLAB_PAGE_SIZE - 1);
    kmem_slab_t* slab = (kmem_slab_t*)(uintptr_t)page_addr;
    if (slab->magic == SLAB_MAGIC && slab->cache) {
        u32 sz = slab->cache->obj_size;
        kmem_cache_free(slab->cache, ptr);
        rust_bridge_mem_on_free((u64)sz);
        return;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Legacy Compatibility APIs
 * ═══════════════════════════════════════════════════════════════════════ */

void* slab_alloc(size_t size) {
    return kmalloc(size);
}

void slab_free(void* ptr, size_t size) {
    (void)size;
    kfree(ptr);
}

u32 slab_class_count(void) {
    return NUM_GENERAL_CACHES;
}

int slab_class_stats(u32 idx, slab_class_stats_t* out) {
    if (!out || idx >= NUM_GENERAL_CACHES) return -1;
    kmem_cache_t* c = s_general_caches[idx];
    if (!c) return -1;
    out->class_size = c->obj_size;
    out->total_objects = c->total_objs;
    out->free_objects = c->total_objs - c->active_objs;
    out->regions = (c->total_objs > 0 && c->objs_per_slab > 0) ? (c->total_objs / c->objs_per_slab) : 0;
    return 0;
}
