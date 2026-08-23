#include "advanced_allocator.h"
#include "slab.h"
#include "heap.h"
#include "frame_allocator.h"

#define ADV_TAG_SLAB 0x534C4142u
#define ADV_TAG_HEAP 0x48454150u
#define ADV_TAG_PAGE 0x50414745u
#define ADV_TAG_PAGE_HEAP 0x50474850u

typedef struct {
    u32 tag;
    u32 alloc_size;
    u16 page_order;
    u16 flags;
} adv_header_t;

typedef struct {
    u32 allocs;
    u32 frees;
    u32 slab_allocs;
    u32 heap_allocs;
    u32 page_allocs;
    u32 failures;
    u32 bytes_live;
    u32 bytes_peak;
} adv_stats_t;

static adv_stats_t g_st;

static u8 order_for_pages(u32 page_count) {
    u8 order = 0u;
    u32 pages = 1u;
    while (pages < page_count && order < 15u) {
        pages <<= 1u;
        order++;
    }
    return order;
}

static u32 order_pages(u8 order) {
    return 1u << order;
}

void advanced_allocator_init(void) {
    slab_init();
    g_st.allocs = 0u;
    g_st.frees = 0u;
    g_st.slab_allocs = 0u;
    g_st.heap_allocs = 0u;
    g_st.page_allocs = 0u;
    g_st.failures = 0u;
    g_st.bytes_live = 0u;
    g_st.bytes_peak = 0u;
}

void* adv_alloc(size_t size) {
    if (size == 0u) return 0;

    size_t total = size + sizeof(adv_header_t);
    adv_header_t* h = 0;

    if (total <= 1024u) {
        h = (adv_header_t*)slab_alloc(total);
        if (!h) {
            g_st.failures++;
            return 0;
        }
        h->tag = ADV_TAG_SLAB;
        h->alloc_size = (u32)total;
        h->page_order = 0u;
        h->flags = 0u;
        g_st.slab_allocs++;
    } else {
        h = (adv_header_t*)heap_alloc(total);
        if (!h) {
            g_st.failures++;
            return 0;
        }
        h->tag = ADV_TAG_HEAP;
        h->alloc_size = (u32)total;
        h->page_order = 0u;
        h->flags = 0u;
        g_st.heap_allocs++;
    }

    g_st.allocs++;
    g_st.bytes_live += (u32)size;
    if (g_st.bytes_live > g_st.bytes_peak) {
        g_st.bytes_peak = g_st.bytes_live;
    }
    return (void*)((u8*)h + sizeof(adv_header_t));
}

void adv_free(void* ptr) {
    if (!ptr) return;

    adv_header_t* h = (adv_header_t*)((u8*)ptr - sizeof(adv_header_t));
    if (h->alloc_size > sizeof(adv_header_t)) {
        u32 user = h->alloc_size - (u32)sizeof(adv_header_t);
        if (g_st.bytes_live >= user) g_st.bytes_live -= user;
        else g_st.bytes_live = 0u;
    }

    if (h->tag == ADV_TAG_SLAB) {
        slab_free((void*)h, h->alloc_size);
    } else if (h->tag == ADV_TAG_HEAP) {
        heap_free((void*)h);
    } else if (h->tag == ADV_TAG_PAGE) {
        frame_free_order((void*)h, (u8)h->page_order);
    } else if (h->tag == ADV_TAG_PAGE_HEAP) {
        heap_free((void*)h);
    }

    g_st.frees++;
}

void* adv_alloc_pages(u32 page_count) {
    if (page_count == 0u) return 0;

    u32 req_bytes = page_count * 4096u;
    u8 order = order_for_pages(page_count);
    void* block = frame_alloc_order(order);
    if (!block) {
        g_st.failures++;
        /* Fallback to heap in severe fragmentation; keeps system alive. */
        adv_header_t* hf = (adv_header_t*)heap_alloc(req_bytes + sizeof(adv_header_t));
        if (!hf) {
            return 0;
        }
        hf->tag = ADV_TAG_PAGE_HEAP;
        hf->alloc_size = req_bytes + (u32)sizeof(adv_header_t);
        hf->page_order = order;
        hf->flags = 2u;

        g_st.allocs++;
        g_st.page_allocs++;
        g_st.bytes_live += req_bytes;
        if (g_st.bytes_live > g_st.bytes_peak) {
            g_st.bytes_peak = g_st.bytes_live;
        }
        return (void*)((u8*)hf + sizeof(adv_header_t));
    }

    adv_header_t* h = (adv_header_t*)block;
    h->tag = ADV_TAG_PAGE;
    h->alloc_size = (order_pages(order) * 4096u);
    h->page_order = order;
    h->flags = 1u;

    g_st.allocs++;
    g_st.page_allocs++;
    g_st.bytes_live += req_bytes;
    if (g_st.bytes_live > g_st.bytes_peak) {
        g_st.bytes_peak = g_st.bytes_live;
    }
    return (void*)((u8*)block + sizeof(adv_header_t));
}

u32 adv_reclaim_step(u32 target_bytes) {
    /* Current reclaim signal: report allocator pressure based on fragmentation + heap score.
       Hook point for future active reclaim/compaction. */
    u32 frag = frame_fragmentation_score();
    u32 heap_frag = heap_fragmentation_score();
    u32 pressure = frag + (heap_frag / 2u);
    if (target_bytes > 0u && pressure > 50u) {
        if (g_st.bytes_live > target_bytes) {
            return g_st.bytes_live - target_bytes;
        }
    }
    return 0u;
}

u32 adv_alloc_failures(void) {
    return g_st.failures;
}
