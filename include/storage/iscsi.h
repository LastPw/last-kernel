/* ==============================================================================
 * LastKernel Enterprise Server Edition — iSCSI SAN Target Protocol Specifications
 * RFC 3720 / RFC 7143 & SCSI Primary Commands (SPC-4) / Block Commands (SBC-3)
 * ============================================================================== */

#ifndef STORAGE_ISCSI_H
#define STORAGE_ISCSI_H

#include "types.h"
#include <stddef.h>

#define ISCSI_PORT                      3260
#define ISCSI_BHS_SIZE                  48
#define ISCSI_MAX_BURST_LENGTH          1048576  /* 1MB Max Burst */
#define ISCSI_FIRST_BURST_LENGTH        262144   /* 256KB First Burst */
#define ISCSI_MAX_RECV_DATA_SEGMENT     262144   /* 256KB Max Segment */
#define ISCSI_DEFAULT_MAX_LUNS          64

/* ── iSCSI Opcode Constants (RFC 7143 §11) ── */
/* Initiator Opcodes */
#define ISCSI_OP_NOP_OUT                0x00
#define ISCSI_OP_SCSI_CMD               0x01
#define ISCSI_OP_TASK_MGT               0x02
#define ISCSI_OP_LOGIN_REQ              0x03
#define ISCSI_OP_TEXT_REQ               0x04
#define ISCSI_OP_DATA_OUT               0x05
#define ISCSI_OP_LOGOUT_REQ             0x06
#define ISCSI_OP_SNACK_REQ              0x10

/* Target Opcodes */
#define ISCSI_OP_NOP_IN                 0x20
#define ISCSI_OP_SCSI_RESP              0x21
#define ISCSI_OP_TASK_MGT_RESP          0x22
#define ISCSI_OP_LOGIN_RESP             0x23
#define ISCSI_OP_TEXT_RESP              0x24
#define ISCSI_OP_DATA_IN                0x25
#define ISCSI_OP_LOGOUT_RESP            0x26
#define ISCSI_OP_R2T                    0x31
#define ISCSI_OP_ASYNC_EVENT            0x32
#define ISCSI_OP_REJECT                 0x3F

/* ── SCSI Block Commands (SBC-3) Opcodes ── */
#define SCSI_CMD_TEST_UNIT_READY        0x00
#define SCSI_CMD_REQUEST_SENSE          0x03
#define SCSI_CMD_INQUIRY                0x12
#define SCSI_CMD_MODE_SELECT_6          0x15
#define SCSI_CMD_MODE_SENSE_6           0x1A
#define SCSI_CMD_START_STOP_UNIT        0x1B
#define SCSI_CMD_READ_CAPACITY_10       0x25
#define SCSI_CMD_READ_10                0x28
#define SCSI_CMD_WRITE_10               0x2A
#define SCSI_CMD_SYNCHRONIZE_CACHE_10   0x35
#define SCSI_CMD_WRITE_SAME_10          0x41
#define SCSI_CMD_UNMAP                  0x42
#define SCSI_CMD_READ_16                0x88
#define SCSI_CMD_WRITE_16               0x8A
#define SCSI_CMD_READ_CAPACITY_16       0x9E
#define SCSI_CMD_REPORT_LUNS            0xA0

/* ── SCSI Sense Keys ── */
#define SENSE_KEY_NO_SENSE              0x00
#define SENSE_KEY_RECOVERED_ERROR       0x01
#define SENSE_KEY_NOT_READY             0x02
#define SENSE_KEY_MEDIUM_ERROR          0x03
#define SENSE_KEY_HARDWARE_ERROR        0x04
#define SENSE_KEY_ILLEGAL_REQUEST       0x05
#define SENSE_KEY_UNIT_ATTENTION        0x06
#define SENSE_KEY_DATA_PROTECT          0x07

/* ── iSCSI Basic Header Segment (BHS) (RFC 7143 §10.2) ── */
typedef struct __attribute__((packed)) {
    u8  opcode;                /* Bit 7: Immediate, Bits 0-5: Opcode */
    u8  flags;                 /* Opcode-specific flags (Final, Transit, etc.) */
    u8  response;              /* Status / Response code */
    u8  qualifier;             /* Qualifier code */
    u8  total_ahs_len;         /* Total AHS Length in 4-byte words */
    u8  data_segment_len[3];   /* Data Segment Length (24-bit integer, Big-Endian) */
    u64 lun;                   /* Logical Unit Number (Big-Endian) */
    u32 itt;                   /* Initiator Task Tag */
    u32 ttt;                   /* Target Transfer Tag / StatSN */
    u32 exp_statsn;            /* Expected Status Sequence Number / CmdSN */
    u32 exp_cmdsn;             /* Expected Command Sequence Number */
    u32 max_cmdsn;             /* Maximum Command Sequence Number */
    u32 datasn;                /* DataSN / R2TSN */
    u32 buffer_offset;         /* Buffer Offset in bytes */
    u32 reserved;
} iscsi_bhs_t;

/* ── iSCSI LUN Backing Store Descriptor ── */
typedef struct {
    u32 lun_id;
    char name[64];
    char device_path[128];     /* e.g. "/dev/nvme0n1p2" or image path */
    u64 size_bytes;
    u32 block_size;            /* 512 or 4096 */
    u64 total_blocks;
    u8  is_read_only;
    u8  is_online;
    u8  naa_id[16];            /* IEEE Registered NAA Identifier */
} iscsi_lun_t;

/* ── iSCSI Target Node Configuration ── */
typedef struct {
    char target_iqn[128];      /* e.g. "iqn.2026-08.corp.axior:storage.target0" */
    char alias[64];
    u32 lun_count;
    iscsi_lun_t luns[ISCSI_DEFAULT_MAX_LUNS];
    u8  auth_method;           /* 0 = None, 1 = CHAP */
    char chap_user[64];
    char chap_secret[64];
    u8  header_digest;         /* 0 = None, 1 = CRC32C */
    u8  data_digest;           /* 0 = None, 1 = CRC32C */
} iscsi_target_t;

/* CRC32C (Castagnoli Polynomial 0x1EDC6F41) for iSCSI Digests */
u32 iscsi_crc32c(const void* data, size_t length);

#endif /* STORAGE_ISCSI_H */
