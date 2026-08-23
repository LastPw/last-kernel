#include "signature_updater.h"
#include "threat_logger.h"

#define SIG_BLOB_MAX 256

static u8 current_blob[SIG_BLOB_MAX];
static u8 backup_blob[SIG_BLOB_MAX];
static u32 current_size = 0;

static u32 checksum32(const u8* data, u32 len) {
    u32 h = 2166136261u;
    for (u32 i = 0; i < len; i++) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

void signature_updater_init(void) {
    current_size = 0;
    for (u32 i = 0; i < SIG_BLOB_MAX; i++) {
        current_blob[i] = 0;
        backup_blob[i] = 0;
    }
}

int signature_updater_apply(const sig_blob_t* blob) {
    if (!blob || !blob->data || blob->size > SIG_BLOB_MAX) {
        threat_logger_log(THREAT_HEAP_CORRUPT, 0, 1, 0);
        return -1;
    }

    u32 s = checksum32(blob->data, blob->size);
    if (s != blob->checksum) {
        threat_logger_log(THREAT_HEAP_CORRUPT, 0, 2, s);
        return -2;
    }

    for (u32 i = 0; i < current_size; i++) {
        backup_blob[i] = current_blob[i];
    }
    u32 backup_size = current_size;

    for (u32 i = 0; i < blob->size; i++) {
        current_blob[i] = blob->data[i];
    }
    current_size = blob->size;

    if (checksum32(current_blob, current_size) != blob->checksum) {
        for (u32 i = 0; i < backup_size; i++) {
            current_blob[i] = backup_blob[i];
        }
        current_size = backup_size;
        threat_logger_log(THREAT_HEAP_CORRUPT, 0, 3, 0);
        return -3;
    }

    return 0;
}
