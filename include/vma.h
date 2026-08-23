#ifndef VMA_H
#define VMA_H

#include "types.h"

#define PROT_NONE       0x0
#define PROT_READ       0x1
#define PROT_WRITE      0x2
#define PROT_EXEC       0x4

#define MAP_SHARED      0x01
#define MAP_PRIVATE     0x02
#define MAP_FIXED       0x10
#define MAP_ANONYMOUS   0x20
#define MAP_ANON        MAP_ANONYMOUS

#define VMA_FLAG_READ     0x01
#define VMA_FLAG_WRITE    0x02
#define VMA_FLAG_EXEC     0x04
#define VMA_FLAG_USER     0x08
#define VMA_FLAG_HUGE2M   0x10
#define VMA_FLAG_HUGE1G   0x20
#define VMA_FLAG_COW      0x40

typedef struct vma_area {
    u64 vm_start;
    u64 vm_end;
    u32 vm_flags;
    u32 task_id;
    i32 fd;
    u64 file_offset;
    struct vma_area* left;
    struct vma_area* right;
    struct vma_area* parent;
    u8 color; /* 0 = RED, 1 = BLACK */
} vma_area_t;

void vma_init(void);
vma_area_t* vma_create(u32 task_id, u64 start, u64 length, u32 flags);
vma_area_t* vma_find(u32 task_id, u64 addr);
int vma_destroy_all(u32 task_id);

/* Standard POSIX memory mapping operations */
u64 vma_mmap(u32 task_id, u64 addr, u64 length, u32 prot, u32 flags, i32 fd, u64 offset);
int vma_munmap(u32 task_id, u64 addr, u64 length);
int vma_mprotect(u32 task_id, u64 addr, u64 length, u32 prot);

#endif /* VMA_H */
