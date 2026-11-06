#include "secure_patch.h"
#include "audit.h"

#define PATCH_REGION_SIZE 1024

static u8 patch_region[PATCH_REGION_SIZE];
static u8 patch_backup[PATCH_REGION_SIZE];

static u32 checksum32(const u8* data, u32 len) {
    u32 h = 2166136261u;
    for (u32 i = 0; i < len; i++) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

void secure_patch_init(void) {
    for (u32 i = 0; i < PATCH_REGION_SIZE; i++) {
        patch_region[i] = 0;
        patch_backup[i] = 0;
    }
}

int secure_patch_apply(const patch_blob_t* patch) {
    if (!patch || !patch->data || patch->size > PATCH_REGION_SIZE) {
        audit_log(AUDIT_EVT_PATCH_FAIL, 1, 0);
        return -1;
    }

    u32 sum = checksum32(patch->data, patch->size);
    if (sum != patch->checksum) {
        audit_log(AUDIT_EVT_PATCH_FAIL, 2, sum);
        return -2;
    }

    for (u32 i = 0; i < PATCH_REGION_SIZE; i++) {
        patch_backup[i] = patch_region[i];
    }

    for (u32 i = 0; i < patch->size; i++) {
        patch_region[i] = patch->data[i];
    }

    if (checksum32(patch_region, patch->size) != patch->checksum) {
        for (u32 i = 0; i < PATCH_REGION_SIZE; i++) {
            patch_region[i] = patch_backup[i];
        }
        audit_log(AUDIT_EVT_PATCH_FAIL, 3, 0);
        return -3;
    }

    audit_log(AUDIT_EVT_PATCH_OK, patch->size, patch->checksum);
    return 0;
}
