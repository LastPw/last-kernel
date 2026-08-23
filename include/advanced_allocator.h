#ifndef ADVANCED_ALLOCATOR_H
#define ADVANCED_ALLOCATOR_H

#include "types.h"
#include "stddef.h"

void advanced_allocator_init(void);
void* adv_alloc(size_t size);
void adv_free(void* ptr);
void* adv_alloc_pages(u32 page_count);
u32 adv_reclaim_step(u32 target_bytes);
u32 adv_alloc_failures(void);

#endif
