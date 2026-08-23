#ifndef LASTFS_H
#define LASTFS_H

#include "types.h"

typedef struct {
    u32 start_lba;
    u32 total_lba;
    u32 boot_src_lba;
    u32 boot_src_count;
    u32 kernel_src_lba;
    u32 kernel_src_count;
} lastfs_seed_plan_t;

int lastfs_format(const lastfs_seed_plan_t* plan);

#endif
