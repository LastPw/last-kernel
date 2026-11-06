/* ==============================================================================
 * LastKernel Enterprise Server Edition — SMB 3.1.1 (CIFS) Protocol Specifications
 * Microsoft Windows & Enterprise Linux High-Performance File Sharing Architecture
 * ============================================================================== */

#ifndef STORAGE_SMB3_H
#define STORAGE_SMB3_H

#include "types.h"
#include <stddef.h>

#define SMB_TCP_PORT            445
#define SMB2_MAGIC              0x424D53FE  /* "\xFESMB" (0xFE, 'S', 'M', 'B') */
#define SMB2_TRANSFORM_MAGIC    0x424D53FD  /* "\xFDSMB" for encrypted frames */

/* ── SMB2 Dialect Revisions ── */
#define SMB2_DIALECT_0202       0x0202
#define SMB2_DIALECT_0210       0x0210
#define SMB2_DIALECT_0300       0x0300
#define SMB2_DIALECT_0302       0x0302
#define SMB2_DIALECT_0311       0x0311  /* SMB 3.1.1 (Standard for Win10/11 & Linux) */

/* ── SMB2 Command Codes (MS-SMB2 §2.2.1.1) ── */
typedef enum {
    SMB2_NEGOTIATE              = 0x0000,
    SMB2_SESSION_SETUP          = 0x0001,
    SMB2_LOGOFF                 = 0x0002,
    SMB2_TREE_CONNECT           = 0x0003,
    SMB2_TREE_DISCONNECT        = 0x0004,
    SMB2_CREATE                 = 0x0005,
    SMB2_CLOSE                  = 0x0006,
    SMB2_FLUSH                  = 0x0007,
    SMB2_READ                   = 0x0008,
    SMB2_WRITE                  = 0x0009,
    SMB2_LOCK                   = 0x000A,
    SMB2_IOCTL                  = 0x000B,
    SMB2_CANCEL                 = 0x000C,
    SMB2_ECHO                   = 0x000D,
    SMB2_QUERY_DIRECTORY        = 0x000E,
    SMB2_CHANGE_NOTIFY          = 0x000F,
    SMB2_QUERY_INFO             = 0x0010,
    SMB2_SET_INFO               = 0x0011,
    SMB2_OPLOCK_BREAK           = 0x0012
} smb2_command_t;

/* ── NT Status Codes (MS-ERREF) ── */
typedef enum {
    STATUS_SUCCESS              = 0x00000000,
    STATUS_PENDING              = 0x00000103,
    STATUS_MORE_PROCESSING_REQUIRED = 0xC0000016,
    STATUS_ACCESS_DENIED        = 0xC0000022,
    STATUS_OBJECT_NAME_NOT_FOUND = 0xC0000034,
    STATUS_OBJECT_NAME_COLLISION = 0xC0000035,
    STATUS_NO_SUCH_FILE         = 0xC000000F,
    STATUS_END_OF_FILE          = 0xC0000011,
    STATUS_DISK_FULL            = 0xC000007F,
    STATUS_INVALID_PARAMETER    = 0xC000000D,
    STATUS_BAD_NETWORK_NAME     = 0xC00000CC,
    STATUS_LOGON_FAILURE        = 0xC000006D,
    STATUS_FILE_CLOSED          = 0xC0000128,
    STATUS_NOT_SUPPORTED        = 0xC00000BB,
    STATUS_INVALID_HANDLE       = 0xC0000008
} nt_status_t;

/* ── Standard 64-Byte SMB2/SMB3 Header ── */
typedef struct __attribute__((packed)) {
    u32 protocol_id;           /* 0x424D53FE */
    u16 structure_size;        /* Always 64 */
    u16 credit_charge;
    u32 status;                /* NT Status code in responses */
    u16 command;               /* smb2_command_t */
    u16 credits;               /* Credits granted / requested */
    u32 flags;                 /* Bit 0: Response (0=Req, 1=Resp), Bit 3: Signed */
    u32 next_command;          /* Offset to next command in compound packet */
    u64 message_id;            /* Monotonically increasing ID */
    u32 process_id;            /* Async / sync process ID */
    u32 tree_id;               /* Connected share handle */
    u64 session_id;            /* Authenticated session handle */
    u8  signature[16];         /* HMAC-SHA256 or AES-CMAC signature */
} smb2_header_t;

/* ── SMB2 NEGOTIATE Request / Response ── */
typedef struct __attribute__((packed)) {
    u16 structure_size;        /* 36 */
    u16 dialect_count;
    u16 security_mode;         /* 0x01: Signing enabled, 0x02: Signing required */
    u16 reserved;
    u32 capabilities;          /* DFS, LEASING, MULTI_CHANNEL, PERSISTENT_HANDLES, DIRECTORY_LEASING, ENCRYPTION */
    u8  client_guid[16];
    u32 negotiate_context_offset;
    u16 negotiate_context_count;
    u16 reserved2;
    /* Dialects array follows */
} smb2_negotiate_req_t;

typedef struct __attribute__((packed)) {
    u16 structure_size;        /* 65 */
    u16 security_mode;
    u16 dialect_revision;      /* e.g. 0x0311 */
    u16 negotiate_context_count;
    u8  server_guid[16];
    u32 capabilities;
    u32 max_transact_size;     /* 1048576 (1MB) */
    u32 max_read_size;         /* 1048576 (1MB) */
    u32 max_write_size;        /* 1048576 (1MB) */
    u64 system_time;           /* Windows FILETIME (100ns intervals since 1601) */
    u64 server_start_time;
    u16 security_buffer_offset;
    u16 security_buffer_length;
    u32 negotiate_context_offset;
} smb2_negotiate_res_t;

/* ── SMB2 TREE_CONNECT Request / Response ── */
typedef struct __attribute__((packed)) {
    u16 structure_size;        /* 9 */
    u16 flags;
    u16 path_offset;           /* Offset to share path e.g. "\\SERVER\SHARE" */
    u16 path_length;
} smb2_tree_connect_req_t;

typedef struct __attribute__((packed)) {
    u16 structure_size;        /* 16 */
    u8  share_type;            /* 0x01 = Physical disk, 0x02 = Pipe, 0x03 = Print */
    u8  reserved;
    u32 share_flags;
    u32 capabilities;
    u32 maximal_access;        /* 0x001F01FF (Full Control) */
} smb2_tree_connect_res_t;

/* ── SMB2 CREATE Request / Response ── */
typedef struct __attribute__((packed)) {
    u16 structure_size;        /* 57 */
    u8  security_flags;
    u8  requested_oplock_level;
    u32 impersonation_level;
    u64 smb_create_flags;
    u64 reserved;
    u32 desired_access;        /* FILE_READ_DATA, FILE_WRITE_DATA, GENERIC_READ, GENERIC_WRITE */
    u32 file_attributes;       /* FILE_ATTRIBUTE_NORMAL, FILE_ATTRIBUTE_DIRECTORY */
    u32 share_access;          /* FILE_SHARE_READ, FILE_SHARE_WRITE, FILE_SHARE_DELETE */
    u32 create_disposition;    /* FILE_SUPERSEDE, FILE_OPEN, FILE_CREATE, FILE_OPEN_IF, FILE_OVERWRITE_IF */
    u32 create_options;
    u16 name_offset;
    u16 name_length;
    u32 create_contexts_offset;
    u32 create_contexts_length;
} smb2_create_req_t;

typedef struct __attribute__((packed)) {
    u16 structure_size;        /* 89 */
    u8  oplock_level;
    u8  flags;
    u32 create_action;         /* FILE_SUPERSEDED, FILE_OPENED, FILE_CREATED, FILE_OVERWRITTEN */
    u64 creation_time;
    u64 last_access_time;
    u64 last_write_time;
    u64 change_time;
    u64 allocation_size;
    u64 end_of_file;           /* File size */
    u32 file_attributes;
    u32 reserved2;
    u64 file_id_persistent;    /* Persistent File Handle */
    u64 file_id_volatile;      /* Volatile File Handle */
    u32 create_contexts_offset;
    u32 create_contexts_length;
} smb2_create_res_t;

/* ── SMB2 READ / WRITE Headers ── */
typedef struct __attribute__((packed)) {
    u16 structure_size;        /* 49 */
    u8  padding;
    u8  flags;
    u32 length;                /* Read length */
    u64 offset;                /* File offset */
    u64 file_id_persistent;
    u64 file_id_volatile;
    u32 minimum_count;
    u32 channel;
    u32 remaining_bytes;
    u16 read_channel_info_offset;
    u16 read_channel_info_length;
} smb2_read_req_t;

typedef struct __attribute__((packed)) {
    u16 structure_size;        /* 17 */
    u8  data_offset;           /* Offset to read data from header start */
    u8  reserved;
    u32 data_length;
    u32 data_remaining;
    u32 reserved2;
} smb2_read_res_t;

typedef struct __attribute__((packed)) {
    u16 structure_size;        /* 49 */
    u16 data_offset;
    u32 length;
    u64 offset;
    u64 file_id_persistent;
    u64 file_id_volatile;
    u32 channel;
    u32 remaining_bytes;
    u16 write_channel_info_offset;
    u16 write_channel_info_length;
    u32 flags;
} smb2_write_req_t;

typedef struct __attribute__((packed)) {
    u16 structure_size;        /* 17 */
    u16 reserved;
    u32 count;                 /* Bytes written */
    u32 remaining;
    u16 write_channel_info_offset;
    u16 write_channel_info_length;
} smb2_write_res_t;

#endif /* STORAGE_SMB3_H */
