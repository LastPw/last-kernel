/* ==============================================================================
 * LastKernel Enterprise Server Edition — NFSv4.2 Protocol Specifications
 * RFC 7530 (NFSv4.0), RFC 8881 (NFSv4.1 Sessions) & RFC 7862 (NFSv4.2) Engine
 * ============================================================================== */

#ifndef STORAGE_NFS4_H
#define STORAGE_NFS4_H

#include "types.h"
#include <stddef.h>

#define NFS4_PORT               2049
#define NFS4_PROGRAM            100003
#define NFS4_VERSION            4
#define NFS4_FHSIZE             128
#define NFS4_VERIFIER_SIZE      8
#define NFS4_STATEID_SIZE       16
#define NFS4_SESSIONID_SIZE     16
#define NFS4_OPAQUE_LIMIT       1024

/* ── NFS4 Status Codes (RFC 7530 §13) ── */
typedef enum {
    NFS4_OK                     = 0,
    NFS4ERR_PERM                = 1,
    NFS4ERR_NOENT               = 2,
    NFS4ERR_IO                  = 5,
    NFS4ERR_NXIO                = 6,
    NFS4ERR_ACCESS              = 13,
    NFS4ERR_EXIST               = 17,
    NFS4ERR_XDEV                = 18,
    NFS4ERR_NOTDIR              = 20,
    NFS4ERR_ISDIR               = 21,
    NFS4ERR_INVAL               = 22,
    NFS4ERR_FBIG                = 27,
    NFS4ERR_NOSPC               = 28,
    NFS4ERR_ROFS                = 30,
    NFS4ERR_MLINK               = 31,
    NFS4ERR_NAMETOOLONG         = 63,
    NFS4ERR_NOTEMPTY            = 66,
    NFS4ERR_DQUOT               = 69,
    NFS4ERR_STALE               = 70,
    NFS4ERR_BADHANDLE           = 10001,
    NFS4ERR_BAD_COOKIE          = 10003,
    NFS4ERR_NOTSUPP             = 10004,
    NFS4ERR_TOOSMALL            = 10005,
    NFS4ERR_SERVERFAULT         = 10006,
    NFS4ERR_BADTYPE             = 10007,
    NFS4ERR_DELAY               = 10008,
    NFS4ERR_SAME                = 10009,
    NFS4ERR_DENIED              = 10010,
    NFS4ERR_EXPIRED             = 10011,
    NFS4ERR_LOCKED              = 10012,
    NFS4ERR_GRACE               = 10013,
    NFS4ERR_FHEXPIRED           = 10014,
    NFS4ERR_SHARE_DENIED        = 10015,
    NFS4ERR_WRONGSEC            = 10016,
    NFS4ERR_CLID_INUSE          = 10017,
    NFS4ERR_RESOURCE            = 10018,
    NFS4ERR_MOVED               = 10019,
    NFS4ERR_NOFILEHANDLE        = 10020,
    NFS4ERR_MINOR_VERS_MISMATCH = 10021,
    NFS4ERR_STALE_CLIENTID      = 10022,
    NFS4ERR_STALE_STATEID       = 10023,
    NFS4ERR_OLD_STATEID         = 10024,
    NFS4ERR_BAD_STATEID         = 10025,
    NFS4ERR_BAD_SEQID           = 10026,
    NFS4ERR_NOT_SAME            = 10027,
    NFS4ERR_LOCK_RANGE          = 10028,
    NFS4ERR_SYMLINK             = 10029,
    NFS4ERR_RESTOREFH           = 10030,
    NFS4ERR_LEASE_MOVED         = 10031,
    NFS4ERR_ATTRNOTSUPP         = 10032,
    NFS4ERR_NO_GRACE            = 10033,
    NFS4ERR_RECLAIM_BAD         = 10034,
    NFS4ERR_RECLAIM_CONFLICT    = 10035,
    NFS4ERR_BADXDR              = 10036,
    NFS4ERR_LOCKS_HELD          = 10037,
    NFS4ERR_OPENMODE            = 10038,
    NFS4ERR_BADOWNER            = 10039,
    NFS4ERR_BADCHAR             = 10040,
    NFS4ERR_BADNAME             = 10041,
    NFS4ERR_BAD_RANGE           = 10042,
    NFS4ERR_LOCK_NOTSUPP        = 10043,
    NFS4ERR_OP_ILLEGAL          = 10044,
    NFS4ERR_DEADLOCK            = 10045,
    NFS4ERR_FILE_OPEN           = 10046,
    NFS4ERR_ADMIN_REVOKED       = 10047,
    NFS4ERR_CB_PATH_DOWN        = 10048,
    /* NFSv4.1 / v4.2 additions */
    NFS4ERR_BADSESSION          = 10052,
    NFS4ERR_BADSLOT             = 10053,
    NFS4ERR_CONN_NOT_BOUND_TO_SESSION = 10055,
    NFS4ERR_SEQUENCE_POS        = 10064,
    NFS4ERR_REQ_TOO_BIG         = 10065,
    NFS4ERR_REP_TOO_BIG         = 10066,
    NFS4ERR_UNION_NOTSUPP       = 10090
} nfsstat4;

/* ── NFS4 COMPOUND Opcodes (RFC 7530 §15 / RFC 8881) ── */
typedef enum {
    OP_ACCESS                   = 3,
    OP_CLOSE                    = 4,
    OP_COMMIT                   = 5,
    OP_CREATE                   = 6,
    OP_DELEGPURGE               = 7,
    OP_DELEGRETURN              = 8,
    OP_GETATTR                  = 9,
    OP_GETFH                    = 10,
    OP_LINK                     = 11,
    OP_LOCK                     = 12,
    OP_LOCKT                    = 13,
    OP_LOCKU                    = 14,
    OP_LOOKUP                   = 15,
    OP_LOOKUPP                  = 16,
    OP_NVERIFY                  = 17,
    OP_OPEN                     = 18,
    OP_OPENATTR                 = 19,
    OP_OPEN_CONFIRM             = 20,
    OP_OPEN_DOWNGRADE           = 21,
    OP_PUTFH                    = 22,
    OP_PUTPUBFH                 = 23,
    OP_PUTROOTFH                = 24,
    OP_READ                     = 25,
    OP_READDIR                  = 26,
    OP_READLINK                 = 27,
    OP_REMOVE                   = 28,
    OP_RENAME                   = 29,
    OP_RENEW                    = 30,
    OP_RESTOREFH                = 31,
    OP_SAVEFH                   = 32,
    OP_SECINFO                  = 33,
    OP_SETATTR                  = 34,
    OP_SETCLIENTID              = 35,
    OP_SETCLIENTID_CONFIRM      = 36,
    OP_VERIFY                   = 37,
    OP_WRITE                    = 38,
    /* NFSv4.1 Sessions Opcodes */
    OP_EXCHANGE_ID              = 42,
    OP_CREATE_SESSION           = 43,
    OP_DESTROY_SESSION          = 44,
    OP_FREE_STATEID             = 45,
    OP_GET_DIR_DELEGATION       = 46,
    OP_GETDEVICEINFO            = 47,
    OP_GETDEVICELIST            = 48,
    OP_LAYOUTCOMMIT             = 49,
    OP_LAYOUTGET                = 50,
    OP_LAYOUTRETURN             = 51,
    OP_SECINFO_NO_NAME          = 52,
    OP_SEQUENCE                 = 53,
    OP_SET_SSV                  = 54,
    OP_TEST_STATEID             = 55,
    OP_WANT_DELEGATION          = 56,
    OP_DESTROY_CLIENTID         = 57,
    OP_RECLAIM_COMPLETE         = 58,
    /* NFSv4.2 Storage Extensions */
    OP_ALLOCATE                 = 59,
    OP_COPY                     = 60,
    OP_COPY_NOTIFY              = 61,
    OP_DEALLOCATE               = 62,
    OP_IO_ADVISE                = 63,
    OP_LAYOUTERROR              = 64,
    OP_LAYOUTSTATS              = 65,
    OP_OFFLOAD_CANCEL           = 66,
    OP_OFFLOAD_STATUS           = 67,
    OP_READ_PLUS                = 68,
    OP_SEEK                     = 69,
    OP_WRITE_SAME               = 70,
    OP_ILLEGAL                  = 10044
} nfs_opnum4;

/* ── NFS4 File Types ── */
typedef enum {
    NF4REG                      = 1,   /* Regular File */
    NF4DIR                      = 2,   /* Directory */
    NF4BLK                      = 3,   /* Block Special Device */
    NF4CHR                      = 4,   /* Character Special Device */
    NF4LNK                      = 5,   /* Symbolic Link */
    NF4SOCK                     = 6,   /* Socket */
    NF4FIFO                     = 7,   /* Named Pipe (FIFO) */
    NF4ATTRDIR                  = 8,   /* Attribute Directory */
    NF4NAMEDATTR                = 9    /* Named Attribute */
} nfs_ftype4;

/* ── NFS4 File Handle Structure ── */
typedef struct {
    u32 len;
    u8  data[NFS4_FHSIZE];
} nfs_fh4;

/* ── NFS4 State ID Structure ── */
typedef struct {
    u32 seqid;
    u8  other[12];
} stateid4;

/* ── NFS4 Compound Request / Response Context ── */
#define NFS4_MAX_COMPOUND_OPS   32

typedef struct {
    u32 opnum;
    /* Decoded Op arguments */
    union {
        struct {
            u32 access_mask;
        } access;
        struct {
            stateid4 stateid;
            u64 seqid;
        } close;
        struct {
            u64 offset;
            u32 count;
            stateid4 stateid;
        } read;
        struct {
            u64 offset;
            u32 stable_how; /* 0 = UNSTABLE4, 1 = DATA_SYNC4, 2 = FILE_SYNC4 */
            stateid4 stateid;
            u32 data_len;
            const u8* data_ptr;
        } write;
        struct {
            char name[256];
        } lookup;
        struct {
            nfs_fh4 fh;
        } putfh;
        struct {
            u32 attr_bitmap[3];
        } getattr;
    } args;
} nfs4_op_arg_t;

typedef struct {
    u32 opnum;
    nfsstat4 status;
    /* Encoded Op results */
    union {
        struct {
            u32 supported;
            u32 access;
        } access;
        struct {
            stateid4 stateid;
        } close;
        struct {
            u8 eof;
            u32 count;
            const u8* data_ptr;
        } read;
        struct {
            u32 count;
            u32 committed;
            u8 verifier[NFS4_VERIFIER_SIZE];
        } write;
        struct {
            nfs_fh4 fh;
        } getfh;
        struct {
            u32 attr_bitmap[3];
            u32 attr_bytes_len;
            u8  attr_bytes[256];
        } getattr;
    } res;
} nfs4_op_res_t;

typedef struct {
    char tag[64];
    u32 minorversion;
    u32 ops_count;
    nfs4_op_arg_t ops[NFS4_MAX_COMPOUND_OPS];
    nfs_fh4 current_fh;
    nfs_fh4 saved_fh;
    u8 has_current_fh;
    u8 has_saved_fh;
} nfs4_compound_req_t;

typedef struct {
    nfsstat4 status;
    char tag[64];
    u32 ops_count;
    nfs4_op_res_t ops[NFS4_MAX_COMPOUND_OPS];
} nfs4_compound_res_t;

#endif /* STORAGE_NFS4_H */
