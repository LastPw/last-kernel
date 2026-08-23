#ifndef HEAP_H
#define HEAP_H

#include "types.h"
#include "stddef.h"

void heap_init(void* start, u32 size);
void* heap_alloc(size_t size);
void heap_free(void* ptr);
u32 heap_used_bytes(void);
u32 heap_peak_bytes(void);
int heap_validate_integrity(void);
int heap_contains(const void* ptr);
u32 heap_corruption_count(void);
u32 heap_fragmentation_score(void);

#endif
