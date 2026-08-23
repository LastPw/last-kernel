#ifndef FRAME_ALLOCATOR_H
#define FRAME_ALLOCATOR_H

#include "types.h"

void frame_allocator_init(u32 ram_bytes);
void* frame_alloc(void);
void frame_free(void* frame);
void* frame_alloc_order(u8 order);
void frame_free_order(void* frame, u8 order);

/* NUMA-aware allocation.  node_count must be set early via
 * frame_set_numa_nodes() before any NUMA-aware calls. */
void frame_set_numa_nodes(u32 count);
u32 frame_numa_nodes(void);
void* frame_alloc_on_node(u32 node_id, u8 order);
u32 frame_total_on_node(u32 node_id);
u32 frame_node_of(void* frame);
#define FRAME_META_ACCESSED    0x01u
#define FRAME_META_DIRTY      0x02u
#define FRAME_META_RECLAIMABLE 0x04u
#define FRAME_META_RESERVED   0x08u
int frame_meta_set(void* frame, u8 flags);
int frame_meta_clear(void* frame, u8 flags);
u8 frame_meta_get(void* frame);
void frame_meta_mark_accessed(void* frame);
void frame_meta_mark_dirty(void* frame);
int frame_ref_inc(void* frame);
u32 frame_ref_dec(void* frame);
u32 frame_ref_get(void* frame);
u32 frame_largest_free_order(void);
u32 frame_fragmentation_score(void);
u32 frame_total(void);
u32 frame_used(void);

#endif
