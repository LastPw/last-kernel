#ifndef SIGNATURE_UPDATER_H
#define SIGNATURE_UPDATER_H

#include "types.h"

typedef struct {
    const u8* data;
    u32 size;
    u32 checksum;
} sig_blob_t;

void signature_updater_init(void);
int signature_updater_apply(const sig_blob_t* blob);

#endif
