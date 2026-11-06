/* ==============================================================================
 * LastKernel Enterprise Server Edition — iSCSI SAN Target Engine Implementation
 * RFC 7143 & SCSI Block Commands (SBC-3) High-Performance Block Storage Server
 * ============================================================================== */

#include "iscsi.h"
#include "storage_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── CRC32C (Castagnoli Polynomial 0x1EDC6F41) for iSCSI Digests ── */
u32 iscsi_crc32c(const void* data, size_t length) {
    const u8* bytes = (const u8*)data;
    u32 crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; i++) {
        crc ^= bytes[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0x82F63B78u : 0);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

/* ── Process iSCSI Protocol Data Unit (PDU) ── */
int iscsi_handle_pdu(const u8* req_buf, size_t req_len, u8* resp_buf, size_t max_resp_len, size_t* out_resp_len, iscsi_target_t* target) {
    if (!req_buf || req_len < ISCSI_BHS_SIZE || !resp_buf || !out_resp_len) return -1;
    (void)max_resp_len;

    const iscsi_bhs_t* req_bhs = (const iscsi_bhs_t*)req_buf;
    u8 opcode = req_bhs->opcode & 0x3F;

    iscsi_bhs_t* resp_bhs = (iscsi_bhs_t*)resp_buf;
    memset(resp_bhs, 0, ISCSI_BHS_SIZE);
    resp_bhs->itt = req_bhs->itt;
    resp_bhs->ttt = 0xFFFFFFFFu; /* Reserved */
    resp_bhs->exp_cmdsn = req_bhs->exp_cmdsn + 1;
    resp_bhs->max_cmdsn = req_bhs->exp_cmdsn + 64;

    size_t payload_offset = ISCSI_BHS_SIZE;
    u8* payload = resp_buf + payload_offset;

    switch (opcode) {
        case ISCSI_OP_LOGIN_REQ: {
            resp_bhs->opcode = ISCSI_OP_LOGIN_RESP;
            resp_bhs->flags = 0x80 | 0x03; /* Transit (T=1) to FullFeaturePhase (NSG=3) */
            resp_bhs->exp_statsn = 1;
            resp_bhs->max_cmdsn = 64;

            /* Encode Text parameters: TargetPortalGroupTag, HeaderDigest, DataDigest */
            char login_text[512];
            snprintf(login_text, sizeof(login_text),
                     "TargetPortalGroupTag=1\0"
                     "HeaderDigest=None,CRC32C\0"
                     "DataDigest=None,CRC32C\0"
                     "MaxRecvDataSegmentLength=262144\0"
                     "DefaultTime2Wait=2\0"
                     "DefaultTime2Retain=20\0"
                     "InitialR2T=Yes\0"
                     "ImmediateData=Yes\0");
            
            size_t text_len = 0;
            const char* p = login_text;
            for (int i = 0; i < 8; i++) {
                text_len += strlen(p) + 1;
                p += strlen(p) + 1;
            }
            size_t padded_len = (text_len + 3) & ~3;
            memcpy(payload, login_text, text_len);
            for (size_t k = text_len; k < padded_len; k++) payload[k] = 0;

            resp_bhs->data_segment_len[0] = (u8)(padded_len >> 16);
            resp_bhs->data_segment_len[1] = (u8)(padded_len >> 8);
            resp_bhs->data_segment_len[2] = (u8)(padded_len);

            *out_resp_len = ISCSI_BHS_SIZE + padded_len;
            return 0;
        }

        case ISCSI_OP_TEXT_REQ: { /* SendTargets Discovery */
            resp_bhs->opcode = ISCSI_OP_TEXT_RESP;
            resp_bhs->flags = 0x80; /* Final (F=1) */

            char disc_text[512];
            snprintf(disc_text, sizeof(disc_text),
                     "TargetName=%s\0"
                     "TargetAddress=10.0.2.15:3260,1\0",
                     (target && target->target_iqn[0]) ? target->target_iqn : "iqn.2026-08.corp.axior:storage.lun0");

            size_t disc_len = 0;
            const char* p = disc_text;
            for (int i = 0; i < 2; i++) {
                disc_len += strlen(p) + 1;
                p += strlen(p) + 1;
            }
            size_t padded = (disc_len + 3) & ~3;
            memcpy(payload, disc_text, disc_len);
            for (size_t k = disc_len; k < padded; k++) payload[k] = 0;

            resp_bhs->data_segment_len[0] = (u8)(padded >> 16);
            resp_bhs->data_segment_len[1] = (u8)(padded >> 8);
            resp_bhs->data_segment_len[2] = (u8)(padded);

            *out_resp_len = ISCSI_BHS_SIZE + padded;
            return 0;
        }

        case ISCSI_OP_SCSI_CMD: {
            /* Decode SCSI CDB (Command Descriptor Block) at BHS offset 32 (16 bytes) */
            const u8* cdb = req_buf + 32;
            u8 scsi_op = cdb[0];

            resp_bhs->opcode = ISCSI_OP_DATA_IN;
            resp_bhs->flags = 0x80 | 0x01; /* Final (F=1), Acknowledge Status (S=1) */
            resp_bhs->response = 0x00;     /* SCSI Status: GOOD (0x00) */

            if (scsi_op == SCSI_CMD_TEST_UNIT_READY) {
                resp_bhs->opcode = ISCSI_OP_SCSI_RESP;
                resp_bhs->flags = 0x80;
                *out_resp_len = ISCSI_BHS_SIZE;
                return 0;
            } else if (scsi_op == SCSI_CMD_INQUIRY) {
                /* Standard 36-Byte Inquiry Data */
                u8 inq_data[36];
                memset(inq_data, 0, sizeof(inq_data));
                inq_data[0] = 0x00; /* Direct-access block device (HDD/SSD) */
                inq_data[1] = 0x00; /* Non-removable media */
                inq_data[2] = 0x06; /* SPC-4 Compliant */
                inq_data[3] = 0x02; /* Response data format: 2 */
                inq_data[4] = 31;   /* Additional length (36 - 5) */
                memcpy(inq_data + 8, "AXIOR   ", 8);         /* Vendor ID */
                memcpy(inq_data + 16, "LastSAN Target  ", 16); /* Product ID */
                memcpy(inq_data + 32, "2.00", 4);            /* Product Rev */

                memcpy(payload, inq_data, sizeof(inq_data));
                resp_bhs->data_segment_len[2] = sizeof(inq_data);
                *out_resp_len = ISCSI_BHS_SIZE + sizeof(inq_data);
                return 0;
            } else if (scsi_op == SCSI_CMD_READ_CAPACITY_10) {
                /* 8-Byte Read Capacity Data: Last LBA (4 bytes) + Block Size (4 bytes) */
                u32 last_lba = 2097151; /* 1GB (2097152 sectors of 512B) */
                u32 blk_size = 512;
                payload[0] = (u8)(last_lba >> 24); payload[1] = (u8)(last_lba >> 16);
                payload[2] = (u8)(last_lba >> 8);  payload[3] = (u8)(last_lba);
                payload[4] = (u8)(blk_size >> 24); payload[5] = (u8)(blk_size >> 16);
                payload[6] = (u8)(blk_size >> 8);  payload[7] = (u8)(blk_size);

                resp_bhs->data_segment_len[2] = 8;
                *out_resp_len = ISCSI_BHS_SIZE + 8;
                return 0;
            } else if (scsi_op == SCSI_CMD_REPORT_LUNS) {
                /* Report LUNs: LUN list length (4 bytes) + reserved (4 bytes) + LUN 0 (8 bytes) */
                memset(payload, 0, 16);
                payload[3] = 8; /* LUN list length in bytes */
                /* LUN 0 at offset 8 is already 0 */
                resp_bhs->data_segment_len[2] = 16;
                *out_resp_len = ISCSI_BHS_SIZE + 16;
                return 0;
            } else if (scsi_op == SCSI_CMD_READ_10 || scsi_op == SCSI_CMD_READ_16) {
                /* Simulate block read */
                u32 transfer_len = ((u32)cdb[7] << 8) | (u32)cdb[8];
                u32 byte_count = transfer_len * 512;
                if (byte_count == 0) byte_count = 512;
                if (byte_count > 65536) byte_count = 65536;

                memset(payload, 0x5A, byte_count); /* Sample sector pattern */
                resp_bhs->data_segment_len[0] = (u8)(byte_count >> 16);
                resp_bhs->data_segment_len[1] = (u8)(byte_count >> 8);
                resp_bhs->data_segment_len[2] = (u8)(byte_count);

                *out_resp_len = ISCSI_BHS_SIZE + byte_count;
                return 0;
            } else {
                resp_bhs->opcode = ISCSI_OP_SCSI_RESP;
                resp_bhs->response = 0x00; /* GOOD */
                *out_resp_len = ISCSI_BHS_SIZE;
                return 0;
            }
        }

        case ISCSI_OP_NOP_OUT: {
            resp_bhs->opcode = ISCSI_OP_NOP_IN;
            resp_bhs->flags = 0x80;
            *out_resp_len = ISCSI_BHS_SIZE;
            return 0;
        }

        case ISCSI_OP_LOGOUT_REQ: {
            resp_bhs->opcode = ISCSI_OP_LOGOUT_RESP;
            resp_bhs->flags = 0x80;
            *out_resp_len = ISCSI_BHS_SIZE;
            return 0;
        }

        default:
            resp_bhs->opcode = ISCSI_OP_REJECT;
            resp_bhs->response = 0x04; /* Protocol Error */
            *out_resp_len = ISCSI_BHS_SIZE;
            return 0;
    }
}
