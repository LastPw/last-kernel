/* ==============================================================================
 * LastKernel Enterprise Server Edition — Unified Storage Manager Subsystem
 * Multi-Protocol Storage Management: NFSv4.2, SMB 3.1.1, iSCSI SAN & ACLs
 * ============================================================================== */

#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include "nfs4.h"
#include "smb3.h"
#include "iscsi.h"

#define STORAGE_MAX_EXPORTS     32
#define STORAGE_MAX_SMB_SHARES  32
#define STORAGE_MAX_TARGETS     16
#define STORAGE_MAX_SESSIONS    128

/* ── NFS Export Configuration Record (/etc/exports) ── */
typedef struct {
    char export_path[256];
    char client_subnet[64];    /* e.g. "10.0.0.0/24" or "*" */
    u8   read_only;
    u8   no_root_squash;
    u8   all_squash;
    u32  anon_uid;
    u32  anon_gid;
    u8   sync_writes;
    u32  export_id;
    u8   is_active;
} nfs_export_rule_t;

/* ── SMB Share Configuration Record (/etc/smb.conf) ── */
typedef struct {
    char share_name[64];       /* e.g. "Public", "Data", "Backups" */
    char share_path[256];      /* e.g. "/mnt/data/public" */
    char comment[128];
    u8   read_only;
    u8   guest_ok;
    u8   browseable;
    char valid_users[128];     /* "root, admin, devops" */
    u32  create_mask;          /* 0755 */
    u32  directory_mask;       /* 0755 */
    u32  share_id;
    u8   is_active;
} smb_share_rule_t;

/* ── Storage Server Live Performance Metrics ── */
typedef struct {
    u64 nfs_requests_total;
    u64 nfs_bytes_read;
    u64 nfs_bytes_written;
    u32 nfs_active_clients;
    
    u64 smb_requests_total;
    u64 smb_bytes_read;
    u64 smb_bytes_written;
    u32 smb_active_sessions;
    
    u64 iscsi_iops_read;
    u64 iscsi_iops_write;
    u64 iscsi_bytes_read;
    u64 iscsi_bytes_written;
    u32 iscsi_active_initiators;
} storage_metrics_t;

/* ── Unified Storage Manager Master State ── */
typedef struct {
    u8  nfs_enabled;
    u8  smb_enabled;
    u8  iscsi_enabled;
    u16 nfs_port;              /* 2049 */
    u16 smb_port;              /* 445 */
    u16 iscsi_port;            /* 3260 */
    
    u32 export_count;
    nfs_export_rule_t exports[STORAGE_MAX_EXPORTS];
    
    u32 smb_share_count;
    smb_share_rule_t smb_shares[STORAGE_MAX_SMB_SHARES];
    
    u32 target_count;
    iscsi_target_t targets[STORAGE_MAX_TARGETS];
    
    storage_metrics_t metrics;
    u8 is_running;
} storage_manager_t;

/* ── Storage Subsystem APIs ── */
int  storage_manager_init(void);
int  storage_manager_load_configs(const char* root_mount);
int  storage_manager_save_configs(const char* root_mount);
int  storage_manager_add_nfs_export(const char* path, const char* client_cidr, u8 read_only, u8 no_root_squash);
int  storage_manager_add_smb_share(const char* name, const char* path, const char* comment, u8 read_only, u8 guest_ok);
int  storage_manager_add_iscsi_target(const char* iqn, const char* alias);
int  storage_manager_add_iscsi_lun(const char* target_iqn, u32 lun_id, const char* name, const char* device_path, u64 size_mb);
int  storage_manager_check_nfs_access(const char* path, const char* client_ip, u8 write_requested);
int  storage_manager_check_smb_access(const char* share_name, const char* username, u8 write_requested);
void storage_manager_get_metrics(storage_metrics_t* out_metrics);

/* Protocol Packet Handlers */
int  nfs4_handle_compound_rpc(const u8* req_buf, size_t req_len, u8* resp_buf, size_t max_resp_len, size_t* out_resp_len, const char* client_ip);
int  smb2_handle_packet(const u8* req_buf, size_t req_len, u8* resp_buf, size_t max_resp_len, size_t* out_resp_len);
int  iscsi_handle_pdu(const u8* req_bhs_and_data, size_t req_len, u8* resp_buf, size_t max_resp_len, size_t* out_resp_len, iscsi_target_t* target);

#endif /* STORAGE_MANAGER_H */
