#ifndef SLAB_H
#define SLAB_H

#include "types.h"
#include "stddef.h"

#define KMEM_CACHE_NAME_LEN 32

typedef struct kmem_cache kmem_cache_t;

typedef struct {
    u32 class_size;
    u32 total_objects;
    u32 free_objects;
    u32 regions;
} slab_class_stats_t;

/* Monolithic Slab Cache Subsystem APIs */
void slab_init(void);
kmem_cache_t* kmem_cache_create(const char* name, size_t size, size_t align, u32 flags);
void* kmem_cache_alloc(kmem_cache_t* cache);
void kmem_cache_free(kmem_cache_t* cache, void* obj);
int kmem_cache_destroy(kmem_cache_t* cache);

/* Predefined Kernel Descriptors Caches */
extern kmem_cache_t* g_task_struct_cache;
extern kmem_cache_t* g_file_cache;

/* General Kernel Dynamic Memory Allocator */
void* kmalloc(size_t size);
void* kzalloc(size_t size);
void  kfree(void* ptr);

/* Legacy Compatibility Slab APIs */
void* slab_alloc(size_t size);
void slab_free(void* ptr, size_t size);
u32 slab_class_count(void);
int slab_class_stats(u32 idx, slab_class_stats_t* out);

#endif /* SLAB_H */
