#ifndef VFS_H
#define VFS_H

#include "types.h"

#define VFS_MAX_NAME 32
#define VFS_MAX_NODES 128
#define VFS_DATA_BLOCK_SIZE 512u
#define VFS_DATA_BLOCK_COUNT 1024u
/* Legacy callers allocate this on the kernel stack. */
#define VFS_FILE_MAX 512u
/* Block-backed files can be much larger than the legacy editor buffer. */
#define VFS_FILE_LIMIT (VFS_DATA_BLOCK_COUNT * VFS_DATA_BLOCK_SIZE)
#define VFS_PATH_MAX 96
#define VFS_JOURNAL_CAP 128

#define VFS_PERM_R 0x4u
#define VFS_PERM_W 0x2u
#define VFS_PERM_X 0x1u
#define VFS_ACC_READ  0x1u
#define VFS_ACC_WRITE 0x2u
#define VFS_ACC_EXEC  0x4u

typedef enum {
    VFS_NODE_NONE = 0,
    VFS_NODE_DIR = 1,
    VFS_NODE_FILE = 2
} vfs_node_type_t;

typedef enum {
    VFS_J_NONE = 0,
    VFS_J_CREATE = 1,
    VFS_J_DELETE = 2,
    VFS_J_WRITE = 3,
    VFS_J_RENAME = 4,
    VFS_J_CHMOD = 5,
    VFS_J_CHOWN = 6,
    VFS_J_LOCK = 7
} vfs_journal_event_t;

typedef struct {
    u8 used;
    u8 locked;
    vfs_node_type_t type;
    u32 id;
    u32 parent_id;
    char name[VFS_MAX_NAME];
    u32 size;
    u32 created_tick;
    u32 modified_tick;
    u32 owner_uid;
    u32 owner_gid;
    u16 mode;
} vfs_node_info_t;

typedef struct {
    u32 total_nodes;
    u32 active_nodes;
    u32 dirs;
    u32 files;
} vfs_stats_t;

typedef struct {
    u32 total_blocks;
    u32 used_blocks;
    u32 referenced_blocks;
    u32 orphan_blocks;
    u32 bad_nodes;
    u32 bad_references;
    u32 journal_entries;
} vfs_fsck_report_t;

typedef struct {
    u32 id;
    u32 tick;
    vfs_journal_event_t event;
    u32 actor_uid;
    u32 node_id;
    u32 parent_id;
    u32 arg0;
    u32 arg1;
    char name[VFS_MAX_NAME];
} vfs_journal_entry_t;

typedef struct {
    int (*node_created)(const vfs_node_info_t* node);
    int (*node_deleted)(u32 node_id, u32 parent_id);
    int (*file_written)(u32 node_id, const char* data, u32 len);
} vfs_backend_ops_t;

void vfs_init(void);
void vfs_load_disk(void);
int vfs_flush_disk(void);
int vfs_rollback_disk(void);
/* Restore the last verified VFS snapshot if a protected system path is missing. */
int vfs_recover_critical(void);
int vfs_fsck(vfs_fsck_report_t* out, u8 repair);
int vfs_usage(u32* total_bytes, u32* used_bytes, u32* free_bytes);
void vfs_set_backend(const vfs_backend_ops_t* ops);
void vfs_begin_batch(void);
int vfs_end_batch(void);
void vfs_set_credentials(u32 uid, u32 gid);
int vfs_mkdir(const char* path);
int vfs_create_file(const char* path);
int vfs_write_file(const char* path, const char* data, u32 len);
int vfs_append_file(const char* path, const char* data, u32 len);
int vfs_read_file(const char* path, char* out, u32 out_len);
int vfs_read_file_at(const char* path, u32 offset, char* out, u32 out_len);
int vfs_delete(const char* path);
int vfs_rename(const char* path, const char* new_name);
int vfs_set_permissions(const char* path, u16 mode);
int vfs_set_owner(const char* path, u32 uid, u32 gid);
int vfs_set_lock(const char* path, u8 locked);
int vfs_access(const char* path, u8 access_mask);
int vfs_info(const char* path, vfs_node_info_t* out);
u32 vfs_list(const char* path, vfs_node_info_t* out, u32 max_out);
void vfs_stats(vfs_stats_t* out);
u32 vfs_journal_count(void);
int vfs_journal_get(u32 reverse_index, vfs_journal_entry_t* out);

#endif
