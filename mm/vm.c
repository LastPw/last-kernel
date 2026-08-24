#include "vm.h"
#include "paging.h"
#include "frame_allocator.h"
#include "scheduler.h"
#include "timer.h"
#include "string.h"

#define VM_MAX_REGIONS 256u
#define VM_PAGE_SIZE 4096u
#define VM_MAX_REGION_PAGES 256u
#define VM_BITMAP_WORDS (VM_MAX_REGION_PAGES / 32u)
#define VM_NULL_FRAME 0xFFFFFFFFu
#define VM_TMP_SRC_VA 0xE0000000u
#define VM_TMP_DST_VA 0xE0001000u
#define VM_DIRECT_MAP_LIMIT (64u * 1024u * 1024u)

typedef struct {
    u8 used;
    u32 id;
    u32 task_id;
    u32 start;
    u32 size;
    u32 prot;
    vm_policy_t policy;
    u32 flags;
    u32 page_count;
    u32 committed_pages;
    u32 max_commit_pages;
    u32 fault_count;
    u32 guard_hits;
    u32 last_fault_tick;
    u32 last_fault_eip;
    u32 page_bitmap[VM_BITMAP_WORDS];
    u32 page_cow_bitmap[VM_BITMAP_WORDS];
    u32 page_frame[VM_MAX_REGION_PAGES];
} vm_region_t;

static vm_region_t g_regions[VM_MAX_REGIONS];
static vm_stats_t g_stats;
static u32 g_next_region_id = 1u;
static u32 g_total_ram = 0u;
static u64 g_zero_frame = VM_NULL_FRAME;

static u32 align_down(u32 v, u32 a) {
    return v & ~(a - 1u);
}

static u32 align_up(u32 v, u32 a) {
    return (v + a - 1u) & ~(a - 1u);
}

static int region_contains(const vm_region_t* r, u32 addr) {
    return (addr >= r->start) && (addr < (r->start + r->size));
}

static int region_guard_hit(const vm_region_t* r, u32 addr) {
    if ((r->flags & VM_FLAG_GUARD_LOW) && addr < (r->start + VM_PAGE_SIZE)) {
        return 1;
    }
    if ((r->flags & VM_FLAG_GUARD_HIGH) && addr >= (r->start + r->size - VM_PAGE_SIZE)) {
        return 1;
    }
    return 0;
}

static int bit_test(const vm_region_t* r, u32 page_idx) {
    u32 w = page_idx / 32u;
    u32 b = page_idx % 32u;
    return (r->page_bitmap[w] & (1u << b)) != 0u;
}

static void bit_set(vm_region_t* r, u32 page_idx) {
    u32 w = page_idx / 32u;
    u32 b = page_idx % 32u;
    r->page_bitmap[w] |= (1u << b);
}

static void bit_clear(vm_region_t* r, u32 page_idx) {
    u32 w = page_idx / 32u;
    u32 b = page_idx % 32u;
    r->page_bitmap[w] &= ~(1u << b);
}

static int cow_test(const vm_region_t* r, u32 page_idx) {
    u32 w = page_idx / 32u;
    u32 b = page_idx % 32u;
    return (r->page_cow_bitmap[w] & (1u << b)) != 0u;
}

static void cow_set(vm_region_t* r, u32 page_idx) {
    u32 w = page_idx / 32u;
    u32 b = page_idx % 32u;
    r->page_cow_bitmap[w] |= (1u << b);
}

static void cow_clear(vm_region_t* r, u32 page_idx) {
    u32 w = page_idx / 32u;
    u32 b = page_idx % 32u;
    r->page_cow_bitmap[w] &= ~(1u << b);
}

static u32 prot_to_page_flags(u32 prot) {
    u32 f = PAGE_FLAG_USER;
    if (prot & VM_PROT_WRITE) {
        f |= PAGE_FLAG_RW;
    }
    if (prot & VM_PROT_EXEC) {
        f |= PAGE_FLAG_EXEC;
    }
    return f;
}

static int can_access(const vm_region_t* r, u32 err_code) {
    u32 write_fault = (err_code & 0x2u) ? 1u : 0u;
    u32 exec_fault = (err_code & 0x10u) ? 1u : 0u;
    if (write_fault && !(r->prot & VM_PROT_WRITE)) {
        return 0;
    }
    if (exec_fault && !(r->prot & VM_PROT_EXEC)) {
        return 0;
    }
    return 1;
}

static int zero_phys_frame(u32 pa) {
    if (pa <= (VM_DIRECT_MAP_LIMIT - VM_PAGE_SIZE) && paging_is_mapped(pa)) {
        memset((void*)(uintptr_t)pa, 0, VM_PAGE_SIZE);
        return 0;
    }
    if (paging_map_page(VM_TMP_DST_VA, pa, PAGE_FLAG_RW) != 0) {
        return -1;
    }
    memset((void*)(uintptr_t)VM_TMP_DST_VA, 0, VM_PAGE_SIZE);
    (void)paging_unmap_page(VM_TMP_DST_VA);
    return 0;
}

static int copy_phys_frame(u32 src_pa, u32 dst_pa) {
    if (src_pa <= (VM_DIRECT_MAP_LIMIT - VM_PAGE_SIZE) &&
        dst_pa <= (VM_DIRECT_MAP_LIMIT - VM_PAGE_SIZE) &&
        paging_is_mapped(src_pa) && paging_is_mapped(dst_pa)) {
        memmove((void*)(uintptr_t)dst_pa, (const void*)(uintptr_t)src_pa, VM_PAGE_SIZE);
        return 0;
    }
    if (paging_map_page(VM_TMP_SRC_VA, src_pa, 0u) != 0) return -1;
    if (paging_map_page(VM_TMP_DST_VA, dst_pa, PAGE_FLAG_RW) != 0) {
        (void)paging_unmap_page(VM_TMP_SRC_VA);
        return -1;
    }
    memmove((void*)(uintptr_t)VM_TMP_DST_VA, (const void*)(uintptr_t)VM_TMP_SRC_VA, VM_PAGE_SIZE);
    (void)paging_unmap_page(VM_TMP_SRC_VA);
    (void)paging_unmap_page(VM_TMP_DST_VA);
    return 0;
}

static int find_region_index(u32 task_id, u32 addr) {
    for (u32 i = 0; i < VM_MAX_REGIONS; i++) {
        if (!g_regions[i].used || g_regions[i].task_id != task_id) {
            continue;
        }
        if (region_contains(&g_regions[i], addr)) {
            return (int)i;
        }
    }
    return -1;
}

static int find_region_slot(u32 task_id, u32 region_id) {
    for (u32 i = 0; i < VM_MAX_REGIONS; i++) {
        if (!g_regions[i].used) continue;
        if (g_regions[i].task_id == task_id && g_regions[i].id == region_id) {
            return (int)i;
        }
    }
    return -1;
}

static int alloc_slot(void) {
    for (u32 i = 0; i < VM_MAX_REGIONS; i++) {
        if (!g_regions[i].used) return (int)i;
    }
    return -1;
}

static void map_region_demand(vm_region_t* r) {
    /* Demand style: pages start non-present, committed on fault. */
    for (u32 p = 0; p < r->page_count; p++) {
        u32 va = r->start + (p * VM_PAGE_SIZE);
        (void)paging_unmap_page(va);
        bit_clear(r, p);
        cow_clear(r, p);
        r->page_frame[p] = VM_NULL_FRAME;
    }
    r->committed_pages = 0u;
}

void vm_init(u32 total_ram_bytes) {
    g_total_ram = total_ram_bytes;
    g_next_region_id = 1u;
    g_stats.total_regions = VM_MAX_REGIONS;
    g_stats.active_regions = 0;
    g_stats.total_faults = 0;
    g_stats.handled_faults = 0;
    g_stats.protection_faults = 0;
    g_stats.guard_faults = 0;
    g_stats.oom_faults = 0;
    g_stats.committed_pages = 0;
    g_stats.cow_maps = 0;
    g_stats.cow_splits = 0;
    g_zero_frame = VM_NULL_FRAME;

    for (u32 i = 0; i < VM_MAX_REGIONS; i++) {
        g_regions[i].used = 0;
    }

    {
        void* zf = frame_alloc();
        if (zf) {
            g_zero_frame = (u32)(uintptr_t)zf;
            (void)zero_phys_frame(g_zero_frame);
        }
    }
}

int vm_create_region(u32 task_id, u32 start, u32 size, u32 prot, vm_policy_t policy, u32 flags) {
    if (size == 0u) return -1;
    start = align_down(start, VM_PAGE_SIZE);
    size = align_up(size, VM_PAGE_SIZE);
    u32 pages = size / VM_PAGE_SIZE;
    if (pages == 0u || pages > VM_MAX_REGION_PAGES) return -1;

    int slot = alloc_slot();
    if (slot < 0) return -1;

    vm_region_t* r = &g_regions[slot];
    r->used = 1;
    r->id = g_next_region_id++;
    r->task_id = task_id;
    r->start = start;
    r->size = size;
    r->prot = prot;
    r->policy = policy;
    if ((prot & VM_PROT_WRITE) && (policy == VM_POLICY_HEAP || policy == VM_POLICY_ANON)) {
        flags |= VM_FLAG_COW;
    }
    r->flags = flags;
    r->page_count = pages;
    r->fault_count = 0u;
    r->guard_hits = 0u;
    r->last_fault_tick = 0u;
    r->last_fault_eip = 0u;
    for (u32 i = 0; i < VM_BITMAP_WORDS; i++) {
        r->page_bitmap[i] = 0u;
        r->page_cow_bitmap[i] = 0u;
    }
    for (u32 i = 0; i < VM_MAX_REGION_PAGES; i++) {
        r->page_frame[i] = VM_NULL_FRAME;
    }

    /* Budget: stack tighter, heap/file wider. */
    if (policy == VM_POLICY_STACK) {
        r->max_commit_pages = (pages > 32u) ? 32u : pages;
    } else if (policy == VM_POLICY_HEAP) {
        r->max_commit_pages = (pages > 192u) ? 192u : pages;
    } else {
        r->max_commit_pages = pages;
    }

    map_region_demand(r);
    g_stats.active_regions++;
    return (int)r->id;
}

int vm_destroy_region(u32 task_id, u32 region_id) {
    int idx = find_region_slot(task_id, region_id);
    if (idx < 0) return -1;
    vm_region_t* r = &g_regions[idx];
    for (u32 p = 0; p < r->page_count; p++) {
        if (r->page_frame[p] != VM_NULL_FRAME) {
            u32 va = r->start + (p * VM_PAGE_SIZE);
            (void)paging_unmap_page(va);
            frame_free((void*)(uintptr_t)r->page_frame[p]);
            r->page_frame[p] = VM_NULL_FRAME;
        }
        cow_clear(r, p);
    }
    if (g_stats.committed_pages >= r->committed_pages) {
        g_stats.committed_pages -= r->committed_pages;
    } else {
        g_stats.committed_pages = 0u;
    }
    r->used = 0;
    if (g_stats.active_regions > 0u) g_stats.active_regions--;
    return 0;
}

void vm_destroy_task_regions(u32 task_id) {
    for (u32 i = 0; i < VM_MAX_REGIONS; i++) {
        if (!g_regions[i].used || g_regions[i].task_id != task_id) continue;
        for (u32 p = 0; p < g_regions[i].page_count; p++) {
            if (g_regions[i].page_frame[p] != VM_NULL_FRAME) {
                u32 va = g_regions[i].start + (p * VM_PAGE_SIZE);
                (void)paging_unmap_page(va);
                frame_free((void*)(uintptr_t)g_regions[i].page_frame[p]);
                g_regions[i].page_frame[p] = VM_NULL_FRAME;
            }
            cow_clear(&g_regions[i], p);
        }
        if (g_stats.committed_pages >= g_regions[i].committed_pages) {
            g_stats.committed_pages -= g_regions[i].committed_pages;
        } else {
            g_stats.committed_pages = 0u;
        }
        g_regions[i].used = 0;
        if (g_stats.active_regions > 0u) g_stats.active_regions--;
    }
}

int vm_register_task_space(u32 task_id, u32 start, u32 end) {
    if (end <= start) return -1;
    u32 size = end - start;
    /* Split into code/data and stack guardable region. */
    u32 mid = start + (size / 2u);
    /* Split as RX code region + RW stack/data region to enforce W^X by design. */
    int a = vm_create_region(task_id, start, mid - start, VM_PROT_READ | VM_PROT_EXEC, VM_POLICY_FILE, 0u);
    int b = vm_create_region(task_id, mid, end - mid, VM_PROT_READ | VM_PROT_WRITE, VM_POLICY_STACK, VM_FLAG_GUARD_LOW | VM_FLAG_GUARD_HIGH);
    return (a > 0 && b > 0) ? 0 : -1;
}

vm_fault_result_t vm_handle_page_fault(u32 task_id, u32 fault_addr, u32 err_code, u32 eip) {
    g_stats.total_faults++;

    int idx = find_region_index(task_id, fault_addr);
    if (idx < 0) {
        return VM_FAULT_UNHANDLED;
    }

    vm_region_t* r = &g_regions[idx];
    u32 write_fault = (err_code & 0x2u) ? 1u : 0u;
    r->fault_count++;
    r->last_fault_tick = timer_ticks();
    r->last_fault_eip = eip;

    if (region_guard_hit(r, fault_addr)) {
        r->guard_hits++;
        g_stats.guard_faults++;
        return VM_FAULT_GUARD;
    }

    if (!can_access(r, err_code)) {
        g_stats.protection_faults++;
        return VM_FAULT_PROTECTION;
    }

    u32 page_idx = (fault_addr - r->start) / VM_PAGE_SIZE;
    if (page_idx >= r->page_count) {
        return VM_FAULT_UNHANDLED;
    }

    if (bit_test(r, page_idx)) {
        if (write_fault && (r->flags & VM_FLAG_COW) && cow_test(r, page_idx) &&
            (r->prot & VM_PROT_WRITE)) {
            u32 va = r->start + (page_idx * VM_PAGE_SIZE);
            u32 old_frame = r->page_frame[page_idx];
            if (old_frame == VM_NULL_FRAME) {
                g_stats.protection_faults++;
                return VM_FAULT_PROTECTION;
            }

            if (frame_ref_get((void*)(uintptr_t)old_frame) > 1u) {
                void* nf = frame_alloc();
                if (!nf) {
                    g_stats.oom_faults++;
                    return VM_FAULT_OOM;
                }
                if (copy_phys_frame(old_frame, (u32)(uintptr_t)nf) != 0) {
                    frame_free(nf);
                    g_stats.oom_faults++;
                    return VM_FAULT_OOM;
                }
                if (paging_map_page(va, (u32)(uintptr_t)nf, prot_to_page_flags(r->prot)) != 0) {
                    frame_free(nf);
                    g_stats.oom_faults++;
                    return VM_FAULT_OOM;
                }
                (void)frame_ref_dec((void*)(uintptr_t)old_frame);
                r->page_frame[page_idx] = (u32)(uintptr_t)nf;
                frame_meta_mark_dirty(nf);
            } else {
                paging_set_page_flags(va, prot_to_page_flags(r->prot));
            }

            cow_clear(r, page_idx);
            g_stats.cow_splits++;
            g_stats.handled_faults++;
            return VM_FAULT_HANDLED;
        }
        g_stats.protection_faults++;
        return VM_FAULT_PROTECTION;
    }

    if (!bit_test(r, page_idx)) {
        if (r->committed_pages >= r->max_commit_pages) {
            g_stats.oom_faults++;
            return VM_FAULT_OOM;
        }
        u32 page_va = r->start + (page_idx * VM_PAGE_SIZE);
        u32 fl = prot_to_page_flags(r->prot);

        if ((r->flags & VM_FLAG_COW) && (r->prot & VM_PROT_WRITE) &&
            !write_fault && g_zero_frame != VM_NULL_FRAME) {
            if (frame_ref_inc((void*)(uintptr_t)g_zero_frame) != 0) {
                g_stats.oom_faults++;
                return VM_FAULT_OOM;
            }
            if (paging_map_page(page_va, g_zero_frame, fl & ~PAGE_FLAG_RW) != 0) {
                (void)frame_ref_dec((void*)(uintptr_t)g_zero_frame);
                g_stats.oom_faults++;
                return VM_FAULT_OOM;
            }
            cow_set(r, page_idx);
            r->page_frame[page_idx] = g_zero_frame;
            frame_meta_mark_accessed((void*)(uintptr_t)g_zero_frame);
            r->committed_pages++;
            g_stats.committed_pages++;
            g_stats.cow_maps++;
            bit_set(r, page_idx);
            g_stats.handled_faults++;
            return VM_FAULT_HANDLED;
        }

        void* frame = frame_alloc();
        if (!frame) {
            g_stats.oom_faults++;
            return VM_FAULT_OOM;
        }
        (void)zero_phys_frame((u32)(uintptr_t)frame);
        if (paging_map_page(page_va, (u32)(uintptr_t)frame, fl) != 0) {
            frame_free(frame);
            g_stats.oom_faults++;
            return VM_FAULT_OOM;
        }
        bit_set(r, page_idx);
        cow_clear(r, page_idx);
        r->page_frame[page_idx] = (u32)(uintptr_t)frame;
        if (write_fault) frame_meta_mark_dirty(frame);
        else frame_meta_mark_accessed(frame);
        r->committed_pages++;
        g_stats.committed_pages++;
    }

    g_stats.handled_faults++;
    return VM_FAULT_HANDLED;
}

int vm_region_info(u32 task_id, u32 region_id, vm_region_info_t* out) {
    if (!out) return -1;
    int idx = find_region_slot(task_id, region_id);
    if (idx < 0) return -1;
    vm_region_t* r = &g_regions[idx];
    out->used = r->used;
    out->id = r->id;
    out->task_id = r->task_id;
    out->start = r->start;
    out->size = r->size;
    out->prot = r->prot;
    out->policy = r->policy;
    out->flags = r->flags;
    out->page_count = r->page_count;
    out->committed_pages = r->committed_pages;
    out->max_commit_pages = r->max_commit_pages;
    out->fault_count = r->fault_count;
    out->guard_hits = r->guard_hits;
    return 0;
}

void vm_stats(vm_stats_t* out) {
    if (!out) return;
    *out = g_stats;
}

void vm_reclaim_tick(void) {
    /* Simple reclaim: under pressure, trim cold committed pages in heap regions. */
    if (g_total_ram == 0u) return;
    if ((g_stats.committed_pages * VM_PAGE_SIZE) < (g_total_ram / 8u)) return;

    for (u32 i = 0; i < VM_MAX_REGIONS; i++) {
        vm_region_t* r = &g_regions[i];
        if (!r->used || r->policy != VM_POLICY_HEAP) continue;
        if (r->committed_pages <= 4u) continue;
        if ((timer_ticks() - r->last_fault_tick) < 200u) continue;

        /* Drop 1 committed page bit at highest index. */
        for (u32 p = r->page_count; p > 0u; p--) {
            u32 pi = p - 1u;
            if (bit_test(r, pi)) {
                u32 va = r->start + (pi * VM_PAGE_SIZE);
                (void)paging_unmap_page(va);
                if (r->page_frame[pi] != VM_NULL_FRAME) {
                    frame_free((void*)(uintptr_t)r->page_frame[pi]);
                    r->page_frame[pi] = VM_NULL_FRAME;
                }
                bit_clear(r, pi);
                cow_clear(r, pi);
                r->committed_pages--;
                if (g_stats.committed_pages > 0u) g_stats.committed_pages--;
                break;
            }
        }
    }
}

int vm_set_region_flags(u32 task_id, u32 region_id, u32 flags_set, u32 flags_clear) {
    int idx = find_region_slot(task_id, region_id);
    if (idx < 0) return -1;

    vm_region_t* r = &g_regions[idx];
    r->flags |= flags_set;
    r->flags &= ~flags_clear;

    if ((flags_clear & VM_FLAG_COW) != 0u) {
        for (u32 p = 0u; p < r->page_count; p++) {
            if (!bit_test(r, p) || !cow_test(r, p)) continue;
            u32 va = r->start + (p * VM_PAGE_SIZE);
            paging_set_page_flags(va, prot_to_page_flags(r->prot));
            cow_clear(r, p);
        }
    }
    return 0;
}
