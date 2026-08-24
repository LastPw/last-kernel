#include "frame_allocator.h"
#include "memory_runtime.h"
#include "spinlock.h"
#include "serial.h"
#include "bootinfo.h"
extern u64 g_kernel_phys_base;

#define FRAME_SIZE 4096u
/* Buddy allocator spanning every usable region the boot loader reports.
 *
 * Per-frame metadata is carved from the tail of the Limine-backed kernel
 * heap (.heap reservation) instead of living in fixed .bss arrays, so the
 * trackable-RAM ceiling scales with a fixed 16 MiB metadata slice rather
 * than an artificial constant: 16 MiB / 10 B-per-frame ~= 1.67 M frames
 * ~= 6.8 GiB.  Installed RAM above that is reported and left to the boot
 * loader; there is no 1 GiB hard cap any more.
 *
 * Returned pointers are PHYSICAL addresses (linear pa-of-pfn encoding).
 * Anything handed to a device for bus-master DMA must come from a region
 * below 4 GiB -- use frame_alloc_dma32_order() for those buffers. */
#define MAX_ORDER 17u            /* 2^17 frames = 512 MiB max buddy block */
#define INVALID_IDX 0xFFFFFFFFu
#define INVALID_ORDER 0xFFu
#define MAX_NUMA_NODES 8u

#define FRAME_DMA32_TOP 0x100000000ull /* 4 GiB bus-address boundary */

/* Pool span target: 512 GiB.  Returned values remain PHYSICAL addresses;
 * every consumer accesses frame contents through the HHDM alias
 * (paging_phys_to_virt), which Limine maps for ALL usable RAM -- so there
 * is no identity-window cap any more. */
#define FA_MAX_SPAN (512ull * 1024ull * 1024ull * 1024ll * 0x1ull)
#define FA_META_PER_FRAME 9u /* next(4)+order(1)+free(1)+ref(2)+meta(1) */

/* ── physical usable regions (from bootinfo parse) ────────────────────── */
#define FRAME_MAX_REGIONS 16u
typedef struct {
    u64 base_pa;           /* physical base of this region */
    u64 len;               /* region length in bytes */
    u32 first_pfn;         /* global pfn of region start */
    u32 frame_count;       /* frames actually tracked here */
} fa_region_t;

static fa_region_t g_reg[FRAME_MAX_REGIONS];
static u32 g_region_count = 0;

static u32 total_frames_count = 0u;
static u32 used_frames_count = 0u;
static u64 g_first_pa = 0;         /* phys addr encoded by pfn 0 */
static u64 g_tracked_bytes = 0;

/* ── per-frame metadata (carved from the top of the largest RAM region,
   accessed through the HHDM alias; sized exactly to tracked frames) ────── */
static u32* next_idx;
static u8*  block_order;
static u8*  block_free;
static u16* frame_refcount;
static u8*  frame_meta;

static spinlock_t g_frame_lock;

static u32 free_head[MAX_ORDER + 1u];

static u32 g_numa_node_count = 1u;
static u32 g_numa_frames_per_node[MAX_NUMA_NODES];

static u32 order_size(u8 order) {
    return 1u << order;
}

/* ── region helpers ───────────────────────────────────────────────────── */

static const fa_region_t* region_of(u32 idx) {
    for (u32 r = 0; r < g_region_count; r++) {
        if (idx >= g_reg[r].first_pfn &&
            idx - g_reg[r].first_pfn < g_reg[r].frame_count) {
            return &g_reg[r];
        }
    }
    return 0;
}

static inline void* pfn_to_va(u32 idx) {
    return (void*)(uintptr_t)((u64)idx * FRAME_SIZE + g_first_pa);
}

static inline u32 va_to_pfn(const void* frame) {
    u64 pa = (u64)(uintptr_t)frame;
    if (pa < g_first_pa) return INVALID_IDX;
    return (u32)((pa - g_first_pa) / FRAME_SIZE);
}

/* ── buddy lists ──────────────────────────────────────────────────────── */

static void list_push(u8 order, u32 idx) {
    next_idx[idx] = free_head[order];
    free_head[order] = idx;
    block_free[idx] = 1u;
    block_order[idx] = order;
}

static void list_remove(u8 order, u32 idx) {
    u32 prev = INVALID_IDX;
    u32 cur = free_head[order];
    while (cur != INVALID_IDX) {
        if (cur == idx) {
            if (prev == INVALID_IDX) {
                free_head[order] = next_idx[cur];
            } else {
                next_idx[prev] = next_idx[cur];
            }
            next_idx[cur] = INVALID_IDX;
            block_free[cur] = 0u;
            return;
        }
        prev = cur;
        cur = next_idx[cur];
    }
}

static int list_contains(u8 order, u32 idx) {
    u32 cur = free_head[order];
    while (cur != INVALID_IDX) {
        if (cur == idx) return 1;
        cur = next_idx[cur];
    }
    return 0;
}

static u32 buddy_of(u32 idx, u8 order) {
    return idx ^ order_size(order);
}

static int same_region(u32 a, u32 b) {
    const fa_region_t* ra = region_of(a);
    const fa_region_t* rb = region_of(b);
    if (ra != rb || ra == 0) return 0;
    return 1;
}

/* Seed free lists from each usable region. Blocks are aligned on their
   global pfn so the XOR-buddy relation holds inside a region; merges never
   cross region boundaries (same_region stops them). */
static void bootstrap_free_space(void) {
    for (u32 r = 0; r < g_region_count; r++) {
        u64 base = (g_reg[r].base_pa + FRAME_SIZE - 1ull) & ~(u64)(FRAME_SIZE - 1ull);
        u64 end = g_reg[r].base_pa + g_reg[r].len;
        end &= ~(u64)(FRAME_SIZE - 1ull);
        while (base < end) {
            u32 idx = (u32)((base - g_first_pa) / FRAME_SIZE);
            u32 remain = (u32)((end - base) / FRAME_SIZE);
            u8 best_order = 0u;
            for (u8 o = 0u; o <= MAX_ORDER; o++) {
                u32 sz = order_size(o);
                if ((idx % sz) == 0u && sz <= remain) best_order = o;
                else break;
            }
            list_push(best_order, idx);
            base += (u64)order_size(best_order) * FRAME_SIZE;
        }
    }
}

static void free_block_raw(u32 idx, u8 order) {
    if (used_frames_count >= order_size(order)) {
        used_frames_count -= order_size(order);
    } else {
        used_frames_count = 0u;
    }

    while (order < MAX_ORDER) {
        u32 buddy = buddy_of(idx, order);
        if (!same_region(idx, buddy)) break;      /* never merge across holes */
        if (!block_free[buddy] || block_order[buddy] != order || !list_contains(order, buddy)) {
            break;
        }
        list_remove(order, buddy);
        if (buddy < idx) idx = buddy;
        order++;
    }

    list_push(order, idx);
}

/* ── init ─────────────────────────────────────────────────────────────── */

void frame_allocator_init(u64 ram_bytes) {
    (void)ram_bytes; /* capacity comes from the bootloader region table */

    /* 1) Collect pool-eligible regions (Limine-fork types: usable=0,
          kernel-and-modules=6), clip to [1 MiB, FA_MAX_SPAN], sort by base. */
    fa_region_t tmp[FRAME_MAX_REGIONS];
    u32 ntmp = 0u;
    u64 span_top = FA_MAX_SPAN;

    for (u32 i = 0; i < g_boot_region_count && ntmp < FRAME_MAX_REGIONS; i++) {
        u64 b = g_boot_regions[i].base;
        u64 l = g_boot_regions[i].len;
        if (l < FRAME_SIZE) continue;
        if (b < FRAME_SIZE) {                     /* 1 MiB floor */
            u64 drop = FRAME_SIZE - b;
            if (drop >= l) continue;
            b = FRAME_SIZE; l -= drop;
        }
        if (b >= span_top) continue;
        if (b + l > span_top) l = span_top - b;
        tmp[ntmp].base_pa = b;
        tmp[ntmp].len = l;
        tmp[ntmp].first_pfn = 0u;
        tmp[ntmp].frame_count = 0u;
        ntmp++;
    }
    /* insertion sort by base */
    for (u32 i = 1; i < ntmp; i++) {
        fa_region_t key = tmp[i];
        u32 j = i;
        while (j > 0u && tmp[j - 1u].base_pa > key.base_pa) {
            tmp[j] = tmp[j - 1u];
            j--;
        }
        tmp[j] = key;
    }

    /* 2) Metadata sizing. */
    u64 raw_frames = 0u;
    for (u32 r = 0; r < ntmp; r++) raw_frames += tmp[r].len / FRAME_SIZE;
    u32 frames = (u32)(raw_frames > (u32)-1 ? (u32)-1 : raw_frames);
    u64 meta_bytes = ((u64)frames * FA_META_PER_FRAME + FRAME_SIZE - 1ull)
                     & ~(u64)(FRAME_SIZE - 1ull);

    if (g_hhdm_offset == 0ull) {
        serial_writeln("[fa] FATAL: no HHDM offset from bootloader");
        return;
    }

    /* Discover the bootloader's live page-table pages by walking CR3:
       this kernel never replaces CR3, so those pages stay in use even
       though the fork labels their whole span "reclaimable".  They are
       excluded from both the metadata carve and the free pools. */
    static u64 tbl_pa[8192];
    u32 tbl_n = 0u;
    {
        u64 cr3 = 0ull;
        __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
        cr3 &= 0x000FFFFFFFFFF000ull;
        tbl_pa[tbl_n++] = cr3;
        u64* pml4 = (u64*)(cr3 + g_hhdm_offset);
        for (u32 i = 0; i < 512 && tbl_n < 8192u; i++) {
            if (!(pml4[i] & 1ull)) continue;
            u64 pdpt_pa = pml4[i] & 0x000FFFFFFFFFF000ull;
            tbl_pa[tbl_n++] = pdpt_pa;
            u64* pdpt = (u64*)(pdpt_pa + g_hhdm_offset);
            for (u32 j = 0; j < 512 && tbl_n < 8192u; j++) {
                u64 e = pdpt[j];
                if (!(e & 1ull) || (e & 0x80ull)) continue;   /* absent / 1G leaf */
                u64 pd_pa = e & 0x000FFFFFFFFFF000ull;
                tbl_pa[tbl_n++] = pd_pa;
                u64* pd = (u64*)(pd_pa + g_hhdm_offset);
                for (u32 k = 0; k < 512 && tbl_n < 8192u; k++) {
                    u64 e2 = pd[k];
                    if (!(e2 & 1ull) || (e2 & 0x80ull)) continue; /* 2M leaf */
                    tbl_pa[tbl_n++] = e2 & 0x000FFFFFFFFFF000ull;
                }
            }
        }
    }

    /* Metadata carve site: lowest span >= meta_bytes that avoids the
       kernel image and every discovered page-table page.  The chosen
       region is split around the carve in the region list. */
    extern u8 __heap_end[];
    extern u8 __kernel_end[];
    u64 img_span = (u64)((uintptr_t)__heap_end - (uintptr_t)__kernel_end);
    if (img_span > 0xC0000000ull) img_span = 0ull;
    u64 img_lo = g_kernel_phys_base;
    u64 img_hi = img_span ? (img_lo + img_span) : 0ull;

    int placed = 0;
    u64 carve_pa = 0ull;
    fa_region_t rebuilt[FRAME_MAX_REGIONS + 2u];
    u32 rn = 0u;
    for (u32 r = 0; r < ntmp && !placed; r++) {
        u64 lo = tmp[r].base_pa;
        u64 hi = tmp[r].base_pa + tmp[r].len;
        u64 cur = (lo + FRAME_SIZE - 1ull) & ~(u64)(FRAME_SIZE - 1ull);
        while (cur + meta_bytes <= hi) {
            int clash = 0;
            if (img_hi != 0ull && cur < img_hi && cur + meta_bytes > img_lo) clash = 1;
            if (!clash) {
                for (u32 t = 0; t < tbl_n; t++) {
                    if (cur < tbl_pa[t] + FRAME_SIZE &&
                        cur + meta_bytes > tbl_pa[t]) { clash = 1; break; }
                }
            }
            if (!clash) { carve_pa = cur; break; }
            cur += FRAME_SIZE;
        }
        if (carve_pa && !placed) {
            u64 tail_lo = carve_pa + meta_bytes;
            for (u32 q = 0; q < ntmp; q++) {
                if (q == r) {
                    if (carve_pa > tmp[q].base_pa) {
                        rebuilt[rn].base_pa = tmp[q].base_pa;
                        rebuilt[rn].len = carve_pa - tmp[q].base_pa;
                        rebuilt[rn].first_pfn = 0u;
                        rebuilt[rn].frame_count = 0u;
                        rn++;
                    }
                    if (tail_lo < hi) {
                        rebuilt[rn].base_pa = tail_lo;
                        rebuilt[rn].len = hi - tail_lo;
                        rebuilt[rn].first_pfn = 0u;
                        rebuilt[rn].frame_count = 0u;
                        rn++;
                    }
                } else {
                    rebuilt[rn++] = tmp[q];
                }
            }
            placed = 1;
        }
    }
    if (!placed) {
        serial_writeln("[fa] FATAL: no safe metadata carve site");
        return;
    }
    for (u32 q = 0; q < rn; q++) tmp[q] = rebuilt[q];
    ntmp = rn;

    next_idx       = (u32*)(carve_pa + g_hhdm_offset);
    block_order    = (u8*)(next_idx + frames);
    block_free     = block_order + frames;
    frame_refcount = (u16*)(block_free + frames);
    frame_meta     = (u8*)(frame_refcount + frames);
    {
        /* carved RAM is not guaranteed zeroed */
        volatile u8* mva = (volatile u8*)(carve_pa + g_hhdm_offset);
        u32 bytes = (u32)(meta_bytes);
        for (u32 i = 0; i < bytes; i++) mva[i] = 0u;
    }

    /* 3) Publish regions with cumulative global pfns; exclude kernel image
       and bootloader page-table pages (img_lo/img_hi/tbl_* from above). */

    g_region_count = 0u;
    total_frames_count = 0u;
    g_first_pa = ntmp ? tmp[0].base_pa : 0ull;
    used_frames_count = 0u;

    for (u32 r = 0; r < ntmp; r++) {
        u32 first = (u32)((tmp[r].base_pa - g_first_pa) / FRAME_SIZE);
        u32 cnt = (u32)(tmp[r].len / FRAME_SIZE);

        /* Frames covered by the kernel image are accounted as used. */
        u32 skipped = 0u;
        if (img_hi != 0ull) {
            u32 s_lo = (u32)(((img_lo + FRAME_SIZE - 1ull) - g_first_pa) / FRAME_SIZE);
            u32 s_hi = (u32)(((img_hi & ~(u64)(FRAME_SIZE - 1ull)) - g_first_pa) / FRAME_SIZE);
            for (u32 p = first; p < first + cnt; p++) {
                if (p >= s_lo && p < s_hi) skipped++;
            }
            if (skipped > cnt) skipped = cnt;
        }

        tmp[r].first_pfn = first;
        tmp[r].frame_count = cnt;
        g_reg[g_region_count++] = tmp[r];
        total_frames_count += cnt;
        used_frames_count += skipped;
        g_tracked_bytes += (u64)cnt * FRAME_SIZE;
    }

    for (u32 o = 0u; o <= MAX_ORDER; o++) free_head[o] = INVALID_IDX;
    bootstrap_free_space();

    /* Recount free frames to derive the true used baseline. */
    {
        u32 free_frames = 0u;
        for (u8 o = 0u; o <= MAX_ORDER; o++) {
            u32 cur = free_head[o];
            while (cur != INVALID_IDX) {
                free_frames += order_size(o);
                cur = next_idx[cur];
            }
        }
        used_frames_count = total_frames_count - free_frames;
    }

    frame_set_numa_nodes(1u);

    serial_write("[fa] regions=");
    serial_write_dec(g_region_count);
    serial_write(" frames=");
    serial_write_dec(total_frames_count);
    serial_write(" gib_x100=");
    serial_write_dec((u32)((g_tracked_bytes * 100ull) >> 30));
    serial_putc('\n');
}

/* ── allocation core ──────────────────────────────────────────────────── */

static void* alloc_from(u8 order, int require_dma32) {
    if (order > MAX_ORDER) return 0;

    u32 flags = spinlock_lock_irqsave(&g_frame_lock);

    u8 cur_order = order;
    u32 idx = INVALID_IDX;
    while (cur_order <= MAX_ORDER) {
        u32 cur = free_head[cur_order];
        while (cur != INVALID_IDX) {
            if (!require_dma32) { idx = cur; break; }
            /* block must sit entirely below the 4 GiB bus boundary */
            if (((u64)cur + order_size(cur_order)) * FRAME_SIZE <= FRAME_DMA32_TOP) {
                idx = cur;
                break;
            }
            cur = next_idx[cur];
        }
        if (idx != INVALID_IDX) break;
        cur_order++;
    }

    if (idx == INVALID_IDX || cur_order > MAX_ORDER) {
        spinlock_unlock_irqrestore(&g_frame_lock, flags);
        (void)memory_runtime_govern_pressure(
            total_frames_count * FRAME_SIZE,
            used_frames_count * FRAME_SIZE,
            frame_fragmentation_score()
        );
        return 0;
    }

    list_remove(cur_order, idx);

    while (cur_order > order) {
        cur_order--;
        u32 right = idx + order_size(cur_order);
        list_push(cur_order, right);
        block_free[idx] = 0u;
        block_order[idx] = cur_order;
    }

    block_order[idx] = order;
    block_free[idx] = 0u;
    {
        u32 n = order_size(order);
        for (u32 i = 0; i < n; i++) {
            frame_refcount[idx + i] = 1u;
            frame_meta[idx + i] = 0u;
        }
    }
    used_frames_count += order_size(order);
    if (used_frames_count > total_frames_count) {
        used_frames_count = total_frames_count;
    }
    void* out = pfn_to_va(idx);
    spinlock_unlock_irqrestore(&g_frame_lock, flags);
    memory_runtime_on_alloc(out, order_size(order) * FRAME_SIZE, 0u, MEMRT_TAG_CACHE);
    return out;
}

void* frame_alloc_order(u8 order) {
    return alloc_from(order, 0);
}

/* Device-DMA-safe allocation: only regions entirely below 4 GiB. */
void* frame_alloc_dma32_order(u8 order) {
    return alloc_from(order, 1);
}

void* frame_alloc(void) {
    return frame_alloc_order(0u);
}

void* frame_alloc_dma32(void) {
    return frame_alloc_dma32_order(0u);
}

/* ── free / refcount ──────────────────────────────────────────────────── */

static int frame_free_order_locked(u32 idx, u8 order_hint) {
    if (idx >= total_frames_count) return 0;

    u8 order = block_order[idx];
    if (order == INVALID_ORDER) order = order_hint;
    if (order > MAX_ORDER) order = 0u;

    if (order == 0u) {
        if (frame_refcount[idx] == 0u) return 0;
        if (frame_refcount[idx] > 1u) {
            frame_refcount[idx]--;
            return 0;
        }
        frame_refcount[idx] = 0u;
        frame_meta[idx] = 0u;
        free_block_raw(idx, 0u);
        return 1;
    }

    u32 n = order_size(order);
    for (u32 i = 0; i < n; i++) {
        if (frame_refcount[idx + i] != 1u) return 0;
    }
    for (u32 i = 0; i < n; i++) {
        frame_refcount[idx + i] = 0u;
        frame_meta[idx + i] = 0u;
    }
    free_block_raw(idx, order);
    return 1;
}

void frame_free_order(void* frame, u8 order_hint) {
    if (!frame) return;
    u32 idx = va_to_pfn(frame);
    if (idx >= total_frames_count || region_of(idx) == 0) return;

    u32 flags = spinlock_lock_irqsave(&g_frame_lock);
    int freed = frame_free_order_locked(idx, order_hint);
    spinlock_unlock_irqrestore(&g_frame_lock, flags);

    if (freed) memory_runtime_on_free(frame, 0u);
}

void frame_free(void* frame) {
    frame_free_order(frame, 0u);
}

int frame_ref_inc(void* frame) {
    if (!frame) return -1;
    u32 idx = va_to_pfn(frame);
    if (idx >= total_frames_count || region_of(idx) == 0) return -1;
    u32 flags = spinlock_lock_irqsave(&g_frame_lock);
    int rc = -1;
    if (frame_refcount[idx] != 0u) {
        if (frame_refcount[idx] < 0xFFFFu) frame_refcount[idx]++;
        rc = 0;
    }
    spinlock_unlock_irqrestore(&g_frame_lock, flags);
    return rc;
}

u32 frame_ref_dec(void* frame) {
    if (!frame) return 0u;
    u32 idx = va_to_pfn(frame);
    if (idx >= total_frames_count || region_of(idx) == 0) return 0u;

    u32 flags = spinlock_lock_irqsave(&g_frame_lock);
    u32 remaining = frame_refcount[idx];
    u32 result = 0u;
    int freed = 0;
    if (remaining > 1u) {
        frame_refcount[idx] = remaining - 1u;
        result = remaining - 1u;
    } else if (remaining == 1u) {
        freed = frame_free_order_locked(idx, 0u);
    }
    spinlock_unlock_irqrestore(&g_frame_lock, flags);

    if (freed) memory_runtime_on_free(frame, 0u);
    return result;
}

u32 frame_ref_get(void* frame) {
    if (!frame) return 0u;
    u32 idx = va_to_pfn(frame);
    if (idx >= total_frames_count || region_of(idx) == 0) return 0u;
    u32 flags = spinlock_lock_irqsave(&g_frame_lock);
    u32 v = frame_refcount[idx];
    spinlock_unlock_irqrestore(&g_frame_lock, flags);
    return v;
}

/* ── per-frame metadata ───────────────────────────────────────────────── */

int frame_meta_set(void* frame, u8 flags) {
    if (!frame) return -1;
    u32 idx = va_to_pfn(frame);
    if (idx >= total_frames_count || region_of(idx) == 0) return -1;
    u32 irq = spinlock_lock_irqsave(&g_frame_lock);
    int rc = -1;
    if (frame_refcount[idx] != 0u) {
        frame_meta[idx] |= flags;
        rc = 0;
    }
    spinlock_unlock_irqrestore(&g_frame_lock, irq);
    return rc;
}

int frame_meta_clear(void* frame, u8 flags) {
    if (!frame) return -1;
    u32 idx = va_to_pfn(frame);
    if (idx >= total_frames_count || region_of(idx) == 0) return -1;
    u32 irq = spinlock_lock_irqsave(&g_frame_lock);
    int rc = -1;
    if (frame_refcount[idx] != 0u) {
        frame_meta[idx] &= (u8)~flags;
        rc = 0;
    }
    spinlock_unlock_irqrestore(&g_frame_lock, irq);
    return rc;
}

u8 frame_meta_get(void* frame) {
    if (!frame) return 0u;
    u32 idx = va_to_pfn(frame);
    if (idx >= total_frames_count || region_of(idx) == 0) return 0u;
    u32 irq = spinlock_lock_irqsave(&g_frame_lock);
    u8 v = frame_meta[idx];
    spinlock_unlock_irqrestore(&g_frame_lock, irq);
    return v;
}

void frame_meta_mark_accessed(void* frame) {
    (void)frame_meta_set(frame, FRAME_META_ACCESSED);
}

void frame_meta_mark_dirty(void* frame) {
    (void)frame_meta_set(frame, FRAME_META_ACCESSED | FRAME_META_DIRTY);
}

/* ── stats ────────────────────────────────────────────────────────────── */

u32 frame_largest_free_order(void) {
    u32 flags = spinlock_lock_irqsave(&g_frame_lock);
    u32 result = 0u;
    for (i32 o = (i32)MAX_ORDER; o >= 0; o--) {
        if (free_head[(u32)o] != INVALID_IDX) {
            result = (u32)o;
            break;
        }
    }
    spinlock_unlock_irqrestore(&g_frame_lock, flags);
    return result;
}

u32 frame_fragmentation_score(void) {
    u32 total_free = total_frames_count - used_frames_count;
    if (total_free == 0u) return 0u;

    u32 largest = order_size((u8)frame_largest_free_order());
    if (largest >= total_free) return 0u;
    return ((total_free - largest) * 100u) / total_free;
}

u32 frame_total(void) {
    return total_frames_count;
}

u32 frame_used(void) {
    return used_frames_count;
}

u64 frame_total_bytes(void) {
    return g_tracked_bytes;
}

/* ── NUMA (index-stripe derivation, no per-frame storage) ─────────────── */

void frame_set_numa_nodes(u32 count) {
    if (count == 0u) count = 1u;
    if (count > MAX_NUMA_NODES) count = MAX_NUMA_NODES;
    g_numa_node_count = count;

    u32 base = 0u;
    for (u32 n = 0u; n < count; n++) {
        u32 per_node = total_frames_count / count;
        if (n == count - 1u) per_node = total_frames_count - base;
        g_numa_frames_per_node[n] = per_node;
        base += per_node;
    }
}

u32 frame_numa_nodes(void) {
    return g_numa_node_count;
}

static u32 node_range(u32 node_id, u32* out_end) {
    u32 start = 0u;
    for (u32 n = 0u; n < node_id; n++) start += g_numa_frames_per_node[n];
    *out_end = start + g_numa_frames_per_node[node_id];
    if (*out_end > total_frames_count) *out_end = total_frames_count;
    return start;
}

void* frame_alloc_on_node(u32 node_id, u8 order) {
    if (order > MAX_ORDER) return 0;
    if (node_id >= g_numa_node_count) node_id = 0u;

    u32 node_start, node_end;
    node_start = node_range(node_id, &node_end);
    u32 block_size = order_size(order);

    u32 flags = spinlock_lock_irqsave(&g_frame_lock);
    void* out = 0;
    u32 alloc_bytes = 0u;

    for (u8 cur_order = order; cur_order <= MAX_ORDER && !out; cur_order++) {
        u32 cur = free_head[cur_order];
        while (cur != INVALID_IDX) {
            if (cur >= node_start && cur + block_size <= node_end) {
                list_remove(cur_order, cur);

                u8 split = cur_order;
                while (split > order) {
                    split--;
                    u32 right = cur + order_size(split);
                    list_push(split, right);
                }

                block_order[cur] = order;
                block_free[cur] = 0u;
                {
                    u32 n = order_size(order);
                    for (u32 i = 0; i < n; i++) {
                        frame_refcount[cur + i] = 1u;
                        frame_meta[cur + i] = 0u;
                    }
                    used_frames_count += n;
                    if (used_frames_count > total_frames_count) {
                        used_frames_count = total_frames_count;
                    }
                    out = pfn_to_va(cur);
                    alloc_bytes = n * FRAME_SIZE;
                }
                break;
            }
            cur = next_idx[cur];
        }
    }
    spinlock_unlock_irqrestore(&g_frame_lock, flags);

    if (!out) {
        return frame_alloc_order(order);
    }

    memory_runtime_on_alloc(out, alloc_bytes, 0u, MEMRT_TAG_CACHE);
    return out;
}

u32 frame_total_on_node(u32 node_id) {
    if (node_id >= g_numa_node_count) return 0u;
    return g_numa_frames_per_node[node_id];
}

u32 frame_node_of(void* frame) {
    if (!frame) return 0u;
    u32 idx = va_to_pfn(frame);
    if (idx >= total_frames_count || region_of(idx) == 0) return 0u;

    for (u32 n = 0u; n < g_numa_node_count; n++) {
        u32 end;
        u32 start = node_range(n, &end);
        if (idx >= start && idx < end) return n;
    }
    return 0u;
}
