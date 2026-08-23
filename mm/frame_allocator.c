#include "frame_allocator.h"
#include "memory_runtime.h"
#include "spinlock.h"
#include "serial.h"

#define FRAME_SIZE 4096u
/* HW-max: 262144 frames covers 1 GiB of direct-mapped RAM; 4 GiB machines
   saturate the allocator at 1 GiB of trackable memory, which the heap,
   userland, and disk caches share.  The old 128 MiB cap was a 32-bit floor.
   MAX_ORDER 17 = 2^17 frames = 512 MiB per buddy block. */
#define MAX_FRAMES 262144u
#define MAX_ORDER 17u /* 2^17 frames = 512 MiB */
#define DIRECT_MAP_LIMIT (1024u * 1024u * 1024u)
#define MAX_NUMA_NODES 8u
#define INVALID_IDX 0xFFFFFFFFu
#define INVALID_ORDER 0xFFu

static u32 total_frames_count = 0u;
static u32 used_frames_count = 0u;

/* Serializes all allocator state.  Taken with IRQs masked: the buddy lists,
   refcounts and block metadata are mutated from both task context and the
   timer IRQ reclaim path, so every public entry point is an atomic critical
   section against interrupt preemption.  Static storage starts zeroed, so
   the lock is unlocked before frame_allocator_init runs. */
static spinlock_t g_frame_lock;

/* Buddy free lists indexed by order, storing block head frame index. */
static u32 free_head[MAX_ORDER + 1u];
static u32 next_idx[MAX_FRAMES];
static u8 block_order[MAX_FRAMES];
static u8 block_free[MAX_FRAMES];
static u16 frame_refcount[MAX_FRAMES];
static u8 frame_meta[MAX_FRAMES];

/* NUMA node tracking: each physical frame belongs to a NUMA node.
 * Frame ranges are divided evenly among detected nodes. */
static u32 g_numa_node_count = 1u;
static u32 g_numa_frames_per_node[MAX_NUMA_NODES];
static u8  g_frame_numa_node[MAX_FRAMES];

static u32 order_size(u8 order) {
    return 1u << order;
}

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

static void bootstrap_free_space(void) {
    /* The kernel image lives in the higher half; its physical address must
       not be derived from the VMA.  Start the pool at 1 MiB: Limine already
       owns the kernel image and its metadata, and everything below 1 MiB is
       reserved for the legacy boot structures. */
    u32 idx = 256u;
    while (idx < total_frames_count) {
        u8 best_order = 0u;
        u32 remain = total_frames_count - idx;
        for (u8 o = 0u; o <= MAX_ORDER; o++) {
            u32 sz = order_size(o);
            if ((idx % sz) == 0u && sz <= remain) {
                best_order = o;
            } else {
                break;
            }
        }
        list_push(best_order, idx);
        idx += order_size(best_order);
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
        if (buddy >= total_frames_count) {
            break;
        }
        if (!block_free[buddy] || block_order[buddy] != order || !list_contains(order, buddy)) {
            break;
        }
        list_remove(order, buddy);
        if (buddy < idx) idx = buddy;
        order++;
    }

    list_push(order, idx);
}

void frame_allocator_init(u32 ram_bytes) {
    if (ram_bytes < (FRAME_SIZE * 512u)) {
        ram_bytes = 64u * 1024u * 1024u;
    }

    total_frames_count = ram_bytes / FRAME_SIZE;
    if (total_frames_count > MAX_FRAMES) {
        total_frames_count = MAX_FRAMES;
    }
    if (total_frames_count > (DIRECT_MAP_LIMIT / FRAME_SIZE)) {
        total_frames_count = DIRECT_MAP_LIMIT / FRAME_SIZE;
    }
    serial_write("[fa] frames=");
    serial_write_dec(total_frames_count);
    serial_putc('\n');
    used_frames_count = total_frames_count;

    for (u32 i = 0u; i <= MAX_ORDER; i++) {
        free_head[i] = INVALID_IDX;
    }
    for (u32 i = 0u; i < MAX_FRAMES; i++) {
        next_idx[i] = INVALID_IDX;
        block_order[i] = INVALID_ORDER;
        block_free[i] = 0u;
        frame_refcount[i] = 0u;
        frame_meta[i] = 0u;
    }

    serial_writeln("[fa] zeroing arrays");
    bootstrap_free_space();
    serial_writeln("[fa] bootstrap done");

    /* Default NUMA topology: a single node owns all frames. */
    g_numa_node_count = 1u;
    g_numa_frames_per_node[0u] = total_frames_count;
    for (u32 i = 0u; i < MAX_FRAMES; i++) {
        g_frame_numa_node[i] = 0u;
    }

    /* Recompute used count from free lists. */
    u32 free_frames = 0u;
    for (u8 o = 0u; o <= MAX_ORDER; o++) {
        u32 cur = free_head[o];
        while (cur != INVALID_IDX) {
            free_frames += order_size(o);
            cur = next_idx[cur];
        }
    }
    if (free_frames > total_frames_count) {
        free_frames = total_frames_count;
    }
    used_frames_count = total_frames_count - free_frames;
}

void* frame_alloc_order(u8 order) {
    if (order > MAX_ORDER) return 0;

    u32 flags = spinlock_lock_irqsave(&g_frame_lock);

    u8 cur_order = order;
    while (cur_order <= MAX_ORDER && free_head[cur_order] == INVALID_IDX) {
        cur_order++;
    }
    if (cur_order > MAX_ORDER) {
        spinlock_unlock_irqrestore(&g_frame_lock, flags);
        (void)memory_runtime_govern_pressure(
            total_frames_count * FRAME_SIZE,
            used_frames_count * FRAME_SIZE,
            frame_fragmentation_score()
        );
        return 0;
    }

    u32 idx = free_head[cur_order];
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
        for (u32 i = 0u; i < n && (idx + i) < MAX_FRAMES; i++) {
            frame_refcount[idx + i] = 1u;
            frame_meta[idx + i] = 0u;
        }
    }
    used_frames_count += order_size(order);
    if (used_frames_count > total_frames_count) {
        used_frames_count = total_frames_count;
    }
    void* out = (void*)(uintptr_t)(idx * (uintptr_t)FRAME_SIZE);
    spinlock_unlock_irqrestore(&g_frame_lock, flags);
    memory_runtime_on_alloc(out, order_size(order) * FRAME_SIZE, 0u, MEMRT_TAG_CACHE);
    return out;
}

void* frame_alloc(void) {
    return frame_alloc_order(0u);
}

/* Free a block back to the buddy allocator with g_frame_lock already held.
   Returns 1 when the frame(s) were actually returned to the free lists — so
   the caller should emit a memory_runtime_on_free notification after
   dropping the lock — and 0 for a no-op: a shared page whose refcount only
   dropped, an already-free frame (double free), or a high-order block that
   is not exclusively owned.  Keeping the runtime callback out of the locked
   section mirrors frame_alloc_order, which notifies after unlocking. */
static int frame_free_order_locked(u32 idx, u8 order_hint) {
    if (idx >= total_frames_count) return 0;

    u8 order = block_order[idx];
    if (order == INVALID_ORDER) {
        order = order_hint;
    }
    if (order > MAX_ORDER) {
        order = 0u;
    }

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

    /* High-order block.  The allocator issues an order-N block with every
       constituent frame at refcount 1, so freeing the unit is only correct
       while that invariant holds.  Validate every frame BEFORE decrementing
       any of them: if a frame is still shared (refcount > 1) or already
       released (refcount 0), returning the block to the buddy lists would
       hand still-referenced memory to a new owner, so we refuse and leave
       the refcounts untouched rather than corrupt the pool. */
    u32 n = order_size(order);
    for (u32 i = 0u; i < n && (idx + i) < total_frames_count; i++) {
        if (frame_refcount[idx + i] != 1u) {
            return 0;
        }
    }
    for (u32 i = 0u; i < n && (idx + i) < total_frames_count; i++) {
        frame_refcount[idx + i] = 0u;
        frame_meta[idx + i] = 0u;
    }
    free_block_raw(idx, order);
    return 1;
}

void frame_free_order(void* frame, u8 order_hint) {
    if (!frame) return;
    u32 idx = (u32)(((uintptr_t)frame) / FRAME_SIZE);
    if (idx >= total_frames_count) return;

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
    u32 idx = (u32)(((uintptr_t)frame) / FRAME_SIZE);
    if (idx >= total_frames_count) return -1;
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
    u32 idx = (u32)(((uintptr_t)frame) / FRAME_SIZE);
    if (idx >= total_frames_count) return 0u;

    /* Decrement and, on the final reference, release the frame under one
       lock acquisition.  Calling the locked helper directly (instead of
       frame_free_order, which re-locks) keeps the whole operation atomic:
       no window where another caller can re-increment the page between the
       "last ref" decision and the buddy-list return. */
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
    u32 idx = (u32)(((uintptr_t)frame) / FRAME_SIZE);
    if (idx >= total_frames_count) return 0u;
    u32 flags = spinlock_lock_irqsave(&g_frame_lock);
    u32 v = frame_refcount[idx];
    spinlock_unlock_irqrestore(&g_frame_lock, flags);
    return v;
}

int frame_meta_set(void* frame, u8 flags) {
    if (!frame) return -1;
    u32 idx = (u32)(((uintptr_t)frame) / FRAME_SIZE);
    if (idx >= total_frames_count) return -1;
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
    u32 idx = (u32)(((uintptr_t)frame) / FRAME_SIZE);
    if (idx >= total_frames_count) return -1;
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
    u32 idx = (u32)(((uintptr_t)frame) / FRAME_SIZE);
    if (idx >= total_frames_count) return 0u;
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
    /* Higher score means more fragmented pool. */
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

/* ── NUMA-aware frame allocation ─────────────────────────────────────── */

void frame_set_numa_nodes(u32 count) {
    if (count == 0u) count = 1u;
    if (count > MAX_NUMA_NODES) count = MAX_NUMA_NODES;
    g_numa_node_count = count;

    /* Divide frames evenly among nodes.  Each node gets a contiguous
     * stripe of physical frame indices. */
    u32 base = 0u;
    for (u32 n = 0u; n < count; n++) {
        u32 per_node = total_frames_count / count;
        if (n == count - 1u) {
            /* Last node gets any remainder. */
            per_node = total_frames_count - base;
        }
        g_numa_frames_per_node[n] = per_node;

        /* Tag every frame in this range with the node id. */
        for (u32 i = 0u; i < per_node && (base + i) < MAX_FRAMES; i++) {
            g_frame_numa_node[base + i] = (u8)n;
        }
        base += per_node;
    }
}

u32 frame_numa_nodes(void) {
    return g_numa_node_count;
}

void* frame_alloc_on_node(u32 node_id, u8 order) {
    if (order > MAX_ORDER) return 0;
    if (node_id >= g_numa_node_count) node_id = 0u;

    /* Calculate the frame index range for this node. */
    u32 node_start = 0u;
    for (u32 n = 0u; n < node_id; n++) {
        node_start += g_numa_frames_per_node[n];
    }
    u32 node_end = node_start + g_numa_frames_per_node[node_id];
    if (node_end > total_frames_count) node_end = total_frames_count;

    u32 block_size = order_size(order);

    u32 flags = spinlock_lock_irqsave(&g_frame_lock);
    void* out = 0;
    u32 alloc_bytes = 0u;

    /* Search the buddy free lists for a block within this node's range.
     * Walk from smallest to largest order for best-fit. */
    for (u8 cur_order = order; cur_order <= MAX_ORDER && !out; cur_order++) {
        u32 cur = free_head[cur_order];
        while (cur != INVALID_IDX) {
            if (cur >= node_start && cur + block_size <= node_end &&
                g_frame_numa_node[cur] == (u8)node_id) {
                /* Found a block in the target node.  Remove and split. */
                list_remove(cur_order, cur);

                /* Split down to the requested order. */
                while (cur_order > order) {
                    cur_order--;
                    u32 right = cur + order_size(cur_order);
                    list_push(cur_order, right);
                }

                block_order[cur] = order;
                block_free[cur] = 0u;
                {
                    u32 n = order_size(order);
                    for (u32 i = 0u; i < n && (cur + i) < MAX_FRAMES; i++) {
                        frame_refcount[cur + i] = 1u;
                        frame_meta[cur + i] = 0u;
                        g_frame_numa_node[cur + i] = (u8)node_id;
                    }
                    used_frames_count += n;
                    if (used_frames_count > total_frames_count) {
                        used_frames_count = total_frames_count;
                    }
                    out = (void*)(uintptr_t)(cur * (uintptr_t)FRAME_SIZE);
                    alloc_bytes = n * FRAME_SIZE;
                }
                break;
            }
            cur = next_idx[cur];
        }
    }
    spinlock_unlock_irqrestore(&g_frame_lock, flags);

    /* Fallback: no free block on the target node.  Allocate from any node.
       frame_alloc_order takes the lock itself, so it must run unlocked. */
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
    u32 idx = (u32)(((uintptr_t)frame) / FRAME_SIZE);
    if (idx >= total_frames_count) return 0u;
    return (u32)g_frame_numa_node[idx];
}
