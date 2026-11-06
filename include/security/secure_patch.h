#ifndef SECURE_PATCH_H
#define SECURE_PATCH_H

#include "types.h"

typedef struct {
    const u8* data;
    u32 size;
    u32 checksum;
} patch_blob_t;

void secure_patch_init(void);
int secure_patch_apply(const patch_blob_t* patch);

#endif
