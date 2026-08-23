#ifndef PERSISTENT_DISK_H
#define PERSISTENT_DISK_H

#include "types.h"

int pdisk_init(void);
int pdisk_ready(void);
int pdisk_save_blob(const char* blob, u32 len);
int pdisk_load_blob(char* out, u32 out_size);
int pdisk_save_vfs_blob(const void* blob, u32 len);
int pdisk_load_vfs_blob(void* out, u32 out_size, u32* out_len);
int pdisk_rollback_vfs(void);
int pdisk_save_panic(const void* record, u32 len);
int pdisk_load_panic(void* record, u32 len);
int pdisk_clear_panic(void);
int pdisk_wal_begin(u32 generation, u32 slot, u32 length, u32 crc);
int pdisk_wal_commit(u32 generation);
int pdisk_wal_recover(void);
int pdisk_wal_clear(void);
int pdisk_test_torn_vfs_wal(void);

#endif
