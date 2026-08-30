#ifndef SHM_H
#define SHM_H

#include "types.h"

typedef struct {
    char name[32];
    u64 phys_addr;
    u32 size_bytes;
    u32 ref_count;
    u8 active;
} shm_region_t;

int shm_init(void);
int shm_open(const char* name, u32 size, u64* out_phys);
int shm_close(const char* name);

#endif
