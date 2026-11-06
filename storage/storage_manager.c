/* ==============================================================================
 * LastKernel Enterprise Server Edition — Unified Storage Manager Subsystem
 * Multi-Protocol Storage Manager: NFSv4.2, SMB 3.1.1, iSCSI Target & ACL Rules
 * ============================================================================== */

#include "storage_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static storage_manager_t s_storage_mgr;
static u8 s_storage_initialized = 0;

int storage_manager_init(void) {
    memset(&s_storage_mgr, 0, sizeof(s_storage_mgr));
    s_storage_mgr.nfs_enabled = 1;
    s_storage_mgr.smb_enabled = 1;
    s_storage_mgr.iscsi_enabled = 1;
    s_storage_mgr.nfs_port = 2049;
    s_storage_mgr.smb_port = 445;
    s_storage_mgr.iscsi_port = 3260;

    /* Add Default Production Storage Exports & Shares */
    storage_manager_add_nfs_export("/mnt/data/public", "*", 0, 1);
    storage_manager_add_nfs_export("/mnt/data/secure", "10.0.0.0/24", 0, 0);

    storage_manager_add_smb_share("Public", "/mnt/data/public", "Public Enterprise Share", 0, 1);
    storage_manager_add_smb_share("Backups", "/mnt/data/backups", "Enterprise Backup Share", 0, 0);

    storage_manager_add_iscsi_target("iqn.2026-08.corp.axior:storage.lun0", "Primary-SAN-Volume");
    storage_manager_add_iscsi_lun("iqn.2026-08.corp.axior:storage.lun0", 0, "SAN-LUN-0", "/dev/nvme0n1p2", 102400);

    s_storage_initialized = 1;
    s_storage_mgr.is_running = 1;
    return 0;
}

int storage_manager_add_nfs_export(const char* path, const char* client_cidr, u8 read_only, u8 no_root_squash) {
    if (!path || s_storage_mgr.export_count >= STORAGE_MAX_EXPORTS) return -1;

    nfs_export_rule_t* exp = &s_storage_mgr.exports[s_storage_mgr.export_count++];
    memset(exp, 0, sizeof(*exp));
    strncpy(exp->export_path, path, sizeof(exp->export_path) - 1);
    strncpy(exp->client_subnet, client_cidr ? client_cidr : "*", sizeof(exp->client_subnet) - 1);
    exp->read_only = read_only;
    exp->no_root_squash = no_root_squash;
    exp->sync_writes = 1;
    exp->export_id = s_storage_mgr.export_count;
    exp->is_active = 1;
    return 0;
}

int storage_manager_add_smb_share(const char* name, const char* path, const char* comment, u8 read_only, u8 guest_ok) {
    if (!name || !path || s_storage_mgr.smb_share_count >= STORAGE_MAX_SMB_SHARES) return -1;

    smb_share_rule_t* sh = &s_storage_mgr.smb_shares[s_storage_mgr.smb_share_count++];
    memset(sh, 0, sizeof(*sh));
    strncpy(sh->share_name, name, sizeof(sh->share_name) - 1);
    strncpy(sh->share_path, path, sizeof(sh->share_path) - 1);
    if (comment) strncpy(sh->comment, comment, sizeof(sh->comment) - 1);
    sh->read_only = read_only;
    sh->guest_ok = guest_ok;
    sh->browseable = 1;
    sh->create_mask = 0755;
    sh->directory_mask = 0755;
    sh->share_id = s_storage_mgr.smb_share_count;
    sh->is_active = 1;
    return 0;
}

int storage_manager_add_iscsi_target(const char* iqn, const char* alias) {
    if (!iqn || s_storage_mgr.target_count >= STORAGE_MAX_TARGETS) return -1;

    iscsi_target_t* tgt = &s_storage_mgr.targets[s_storage_mgr.target_count++];
    memset(tgt, 0, sizeof(*tgt));
    strncpy(tgt->target_iqn, iqn, sizeof(tgt->target_iqn) - 1);
    if (alias) strncpy(tgt->alias, alias, sizeof(tgt->alias) - 1);
    tgt->header_digest = 1; /* CRC32C enabled */
    tgt->data_digest = 1;
    return 0;
}

int storage_manager_add_iscsi_lun(const char* target_iqn, u32 lun_id, const char* name, const char* device_path, u64 size_mb) {
    if (!target_iqn || !name || !device_path) return -1;

    iscsi_target_t* target = NULL;
    for (u32 i = 0; i < s_storage_mgr.target_count; i++) {
        if (strcmp(s_storage_mgr.targets[i].target_iqn, target_iqn) == 0) {
            target = &s_storage_mgr.targets[i];
            break;
        }
    }
    if (!target || target->lun_count >= ISCSI_DEFAULT_MAX_LUNS) return -2;

    iscsi_lun_t* lun = &target->luns[target->lun_count++];
    memset(lun, 0, sizeof(*lun));
    lun->lun_id = lun_id;
    strncpy(lun->name, name, sizeof(lun->name) - 1);
    strncpy(lun->device_path, device_path, sizeof(lun->device_path) - 1);
    lun->size_bytes = size_mb * 1024ULL * 1024ULL;
    lun->block_size = 512;
    lun->total_blocks = lun->size_bytes / lun->block_size;
    lun->is_online = 1;
    return 0;
}

int storage_manager_check_nfs_access(const char* path, const char* client_ip, u8 write_requested) {
    if (!path || !client_ip) return -1;
    if (!s_storage_initialized) storage_manager_init();

    for (u32 i = 0; i < s_storage_mgr.export_count; i++) {
        const nfs_export_rule_t* exp = &s_storage_mgr.exports[i];
        if (!exp->is_active) continue;

        if (strcmp(exp->export_path, path) == 0) {
            if (exp->read_only && write_requested) {
                return -2; /* Read-only export violation */
            }
            if (strcmp(exp->client_subnet, "*") == 0) {
                return 0; /* Unrestricted access */
            }
            /* Subnet prefix check */
            if (strncmp(client_ip, exp->client_subnet, strlen(exp->client_subnet) - 3) == 0) {
                return 0;
            }
        }
    }
    return 0;
}

int storage_manager_check_smb_access(const char* share_name, const char* username, u8 write_requested) {
    if (!share_name) return -1;
    if (!s_storage_initialized) storage_manager_init();

    for (u32 i = 0; i < s_storage_mgr.smb_share_count; i++) {
        const smb_share_rule_t* sh = &s_storage_mgr.smb_shares[i];
        if (!sh->is_active) continue;

        if (strcasecmp(sh->share_name, share_name) == 0) {
            if (sh->read_only && write_requested) return -2;
            if (sh->guest_ok) return 0;
            if (username && sh->valid_users[0]) {
                if (strstr(sh->valid_users, username)) return 0;
            }
            return 0;
        }
    }
    return -3;
}

void storage_manager_get_metrics(storage_metrics_t* out_metrics) {
    if (!out_metrics) return;
    if (!s_storage_initialized) storage_manager_init();
    *out_metrics = s_storage_mgr.metrics;
}

int storage_manager_save_configs(const char* root_mount) {
    if (!root_mount) return -1;
    char path[512];

    /* 1. Write /etc/exports */
    snprintf(path, sizeof(path), "%s/etc/exports", root_mount);
    FILE* f_exp = fopen(path, "w");
    if (f_exp) {
        fprintf(f_exp, "# /etc/exports: NFSv4 Export Rules for LastKernel Server\n");
        for (u32 i = 0; i < s_storage_mgr.export_count; i++) {
            const nfs_export_rule_t* e = &s_storage_mgr.exports[i];
            fprintf(f_exp, "%s    %s(%s,%s,sync,no_subtree_check)\n",
                    e->export_path, e->client_subnet,
                    e->read_only ? "ro" : "rw",
                    e->no_root_squash ? "no_root_squash" : "root_squash");
        }
        fclose(f_exp);
    }

    /* 2. Write /etc/smb.conf */
    snprintf(path, sizeof(path), "%s/etc/smb.conf", root_mount);
    FILE* f_smb = fopen(path, "w");
    if (f_smb) {
        fprintf(f_smb,
            "[global]\n"
            "   workgroup = WORKGROUP\n"
            "   server string = LastKernel Enterprise File Server\n"
            "   server min protocol = SMB2_02\n"
            "   server max protocol = SMB3_11\n"
            "   security = user\n"
            "   map to guest = Bad User\n\n");
        for (u32 i = 0; i < s_storage_mgr.smb_share_count; i++) {
            const smb_share_rule_t* s = &s_storage_mgr.smb_shares[i];
            fprintf(f_smb,
                "[%s]\n"
                "   path = %s\n"
                "   comment = %s\n"
                "   read only = %s\n"
                "   guest ok = %s\n"
                "   browseable = %s\n\n",
                s->share_name, s->share_path, s->comment,
                s->read_only ? "yes" : "no",
                s->guest_ok ? "yes" : "no",
                s->browseable ? "yes" : "no");
        }
        fclose(f_smb);
    }

    return 0;
}
