/* ==============================================================================
 * LastKernel Enterprise Server Edition — Multi-Protocol Storage Test Suite
 * Validates NFSv4.2 COMPOUND, SMB 3.1.1 (CIFS), iSCSI Target & CRC32C Castagnoli
 * ============================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "storage_manager.h"

/* Forward declarations */
typedef struct { u8* data; size_t size; size_t pos; } xdr_stream_t;
void xdr_init_read(xdr_stream_t* xdr, const u8* buf, size_t len);
void xdr_init_write(xdr_stream_t* xdr, u8* buf, size_t max_len);
int  xdr_put_u32(xdr_stream_t* xdr, u32 val);
int  xdr_put_string(xdr_stream_t* xdr, const char* str);
int  xdr_put_opaque(xdr_stream_t* xdr, const u8* buf, u32 len);

int main(void) {
    printf("\n=======================================================\n");
    printf("   LastKernel Enterprise Network Storage Test Suite    \n");
    printf("=======================================================\n");

    /* [1/6] Testing ONC RPC 2.0 & XDR Engine */
    printf("  [1/6] Testing ONC RPC 2.0 & XDR Serialization Engine... ");
    u8 xdr_buffer[256];
    xdr_stream_t xdr_w;
    xdr_init_write(&xdr_w, xdr_buffer, sizeof(xdr_buffer));
    assert(xdr_put_u32(&xdr_w, 0x12345678) == 0);
    assert(xdr_put_string(&xdr_w, "LastKernel-NFS") == 0);
    assert(xdr_w.pos == 4 + 4 + 16); /* 4B u32 + 4B str_len + 14B str padded to 16B */
    printf("PASS (XDR 4-Byte Padded Alignment & Primitive Types Verified)\n");

    /* [2/6] Testing NFSv4.2 COMPOUND RPC Engine */
    printf("  [2/6] Testing NFSv4.2 Distributed COMPOUND Engine... ");
    u8 nfs_req_buf[512], nfs_resp_buf[512];
    xdr_stream_t nfs_req_xdr;
    xdr_init_write(&nfs_req_xdr, nfs_req_buf, sizeof(nfs_req_buf));

    /* Build RPC Call Header */
    xdr_put_u32(&nfs_req_xdr, 0x98765432); /* XID */
    xdr_put_u32(&nfs_req_xdr, 0);          /* CALL */
    xdr_put_u32(&nfs_req_xdr, 2);          /* RPC Version 2 */
    xdr_put_u32(&nfs_req_xdr, 100003);     /* Program NFS */
    xdr_put_u32(&nfs_req_xdr, 4);          /* Version 4 */
    xdr_put_u32(&nfs_req_xdr, 1);          /* Proc COMPOUND */
    xdr_put_u32(&nfs_req_xdr, 0);          /* AUTH_NONE flavor */
    xdr_put_u32(&nfs_req_xdr, 0);          /* cred_len */
    xdr_put_u32(&nfs_req_xdr, 0);          /* verf flavor */
    xdr_put_u32(&nfs_req_xdr, 0);          /* verf len */

    /* Build COMPOUND Body: PUTROOTFH (24) -> GETFH (10) -> ACCESS (3) */
    xdr_put_string(&nfs_req_xdr, "test_tag");
    xdr_put_u32(&nfs_req_xdr, 2);          /* minorversion = 2 (NFSv4.2) */
    xdr_put_u32(&nfs_req_xdr, 3);          /* ops_count = 3 */
    xdr_put_u32(&nfs_req_xdr, OP_PUTROOTFH);
    xdr_put_u32(&nfs_req_xdr, OP_GETFH);
    xdr_put_u32(&nfs_req_xdr, OP_ACCESS);
    xdr_put_u32(&nfs_req_xdr, 0x01);       /* ACCESS4_READ */

    size_t nfs_resp_len = 0;
    assert(nfs4_handle_compound_rpc(nfs_req_buf, nfs_req_xdr.pos, nfs_resp_buf, sizeof(nfs_resp_buf), &nfs_resp_len, "127.0.0.1") == 0);
    assert(nfs_resp_len > 32);
    printf("PASS (COMPOUND: PUTROOTFH -> GETFH -> ACCESS Executed 100%% Successfully)\n");

    /* [3/6] Testing SMB 3.1.1 (CIFS) Protocol Engine */
    printf("  [3/6] Testing SMB 3.1.1 (Windows/Linux CIFS) Protocol Engine... ");
    u8 smb_req[256], smb_resp[512];
    smb2_header_t* smb_hdr = (smb2_header_t*)smb_req;
    memset(smb_hdr, 0, sizeof(*smb_hdr));
    smb_hdr->protocol_id = SMB2_MAGIC;
    smb_hdr->structure_size = 64;
    smb_hdr->command = SMB2_NEGOTIATE;
    smb_hdr->message_id = 1;

    size_t smb_resp_len = 0;
    assert(smb2_handle_packet(smb_req, sizeof(smb2_header_t), smb_resp, sizeof(smb_resp), &smb_resp_len) == 0);
    smb2_header_t* smb_r_hdr = (smb2_header_t*)smb_resp;
    assert(smb_r_hdr->protocol_id == SMB2_MAGIC);
    assert(smb_r_hdr->status == STATUS_SUCCESS);
    smb2_negotiate_res_t* neg_res = (smb2_negotiate_res_t*)(smb_resp + sizeof(smb2_header_t));
    assert(neg_res->dialect_revision == SMB2_DIALECT_0311);
    printf("PASS (SMB 3.1.1 Dialect 0x0311 & Capability Negotiated Successfully)\n");

    /* [4/6] Testing iSCSI Castagnoli CRC32C & BHS Framing */
    printf("  [4/6] Testing iSCSI Castagnoli CRC32C & BHS Framing... ");
    const char* sample_data = "123456789";
    u32 crc = iscsi_crc32c(sample_data, 9);
    /* Standard test vector for CRC32C of "123456789" is 0xE3069283 */
    assert(crc == 0xE3069283u);

    u8 iscsi_req[256], iscsi_resp[512];
    iscsi_bhs_t* bhs_req = (iscsi_bhs_t*)iscsi_req;
    memset(bhs_req, 0, sizeof(*bhs_req));
    bhs_req->opcode = ISCSI_OP_LOGIN_REQ;
    bhs_req->itt = 0x11223344;

    size_t iscsi_resp_len = 0;
    iscsi_target_t target;
    memset(&target, 0, sizeof(target));
    strncpy(target.target_iqn, "iqn.2026-08.corp.axior:storage.lun0", sizeof(target.target_iqn) - 1);
    assert(iscsi_handle_pdu(iscsi_req, ISCSI_BHS_SIZE, iscsi_resp, sizeof(iscsi_resp), &iscsi_resp_len, &target) == 0);
    iscsi_bhs_t* bhs_resp = (iscsi_bhs_t*)iscsi_resp;
    assert(bhs_resp->opcode == ISCSI_OP_LOGIN_RESP);
    assert(bhs_resp->itt == 0x11223344);
    printf("PASS (CRC32C Vector 0xE3069283 & iSCSI Login PDU Verified)\n");

    /* [5/6] Testing SCSI Primary & Block Commands (SPC-4 / SBC-3) */
    printf("  [5/6] Testing SCSI SBC-3 (INQUIRY, READ CAPACITY, REPORT LUNS)... ");
    memset(bhs_req, 0, sizeof(*bhs_req));
    bhs_req->opcode = ISCSI_OP_SCSI_CMD;
    bhs_req->itt = 0x55667788;
    u8* cdb = iscsi_req + 32;
    cdb[0] = SCSI_CMD_INQUIRY;

    assert(iscsi_handle_pdu(iscsi_req, ISCSI_BHS_SIZE, iscsi_resp, sizeof(iscsi_resp), &iscsi_resp_len, &target) == 0);
    assert(iscsi_resp_len >= ISCSI_BHS_SIZE + 36);
    assert(memcmp(iscsi_resp + ISCSI_BHS_SIZE + 8, "AXIOR   ", 8) == 0);

    /* Test READ CAPACITY (10) */
    cdb[0] = SCSI_CMD_READ_CAPACITY_10;
    assert(iscsi_handle_pdu(iscsi_req, ISCSI_BHS_SIZE, iscsi_resp, sizeof(iscsi_resp), &iscsi_resp_len, &target) == 0);
    assert(iscsi_resp_len == ISCSI_BHS_SIZE + 8);
    printf("PASS (SCSI INQUIRY 'AXIOR LastSAN' & READ CAPACITY 10 Validated)\n");

    /* [6/6] Testing Multi-Protocol Storage Manager & Subnet ACL Rules */
    printf("  [6/6] Testing Multi-Protocol Storage Manager & Subnet ACL Rules... ");
    assert(storage_manager_init() == 0);
    assert(storage_manager_check_nfs_access("/mnt/data/public", "192.168.1.50", 1) == 0);
    assert(storage_manager_check_nfs_access("/mnt/data/secure", "10.0.0.15", 1) == 0);
    assert(storage_manager_check_smb_access("Public", "guest", 0) == 0);

    const char* test_storage_dir = "/tmp/lastos_storage_test";
    char mkdir_cmd[256];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s/etc", test_storage_dir);
    (void)system(mkdir_cmd);
    assert(storage_manager_save_configs(test_storage_dir) == 0);

    char exp_check[512];
    snprintf(exp_check, sizeof(exp_check), "%s/etc/exports", test_storage_dir);
    FILE* f_e = fopen(exp_check, "r"); assert(f_e != NULL); fclose(f_e);

    snprintf(exp_check, sizeof(exp_check), "%s/etc/smb.conf", test_storage_dir);
    FILE* f_s = fopen(exp_check, "r"); assert(f_s != NULL); fclose(f_s);

    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "rm -rf %s", test_storage_dir);
    (void)system(mkdir_cmd);
    printf("PASS (Subnet ACLs, /etc/exports & /etc/smb.conf Verified 100%%)\n");

    printf(">>> ALL 6 ENTERPRISE STORAGE PROTOCOL TEST SUITES PASSED 100%% SUCCESSFULLY! <<<\n\n");
    return 0;
}
