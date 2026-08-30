#ifndef IO_URING_H
#define IO_URING_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 *                    LastKernel High-Performance io_uring
 *         Asynchronous I/O Framework for Storage & Network Sockets
 * ============================================================================
 */

/* ─── Submission Queue Entry (SQE) ────────────────────────────────────────── */
typedef struct io_uring_sqe {
    u8      opcode;         /* IORING_OP_* operation code */
    u8      flags;          /* IOSQE_* modifier flags */
    u16     ioprio;         /* I/O priority level */
    i32     fd;             /* Target file / socket descriptor */
    union {
        u64 off;            /* Offset into file */
        u64 addr2;          /* Secondary address for specific ops */
    };
    union {
        u64 addr;           /* Pointer to buffer / struct iovec / sockaddr */
        u64 splice_off_in;  /* Input splice offset */
    };
    u32     len;            /* Buffer length or vector count */
    union {
        u32 rw_flags;       /* RWF_* flags for read/write */
        u32 fsync_flags;    /* IORING_FSYNC_DATASYNC */
        u16 poll_events;    /* POLLIN, POLLOUT, etc. */
        u32 sync_range_flags;
        u32 msg_flags;
        u32 timeout_flags;
        u32 accept_flags;
        u32 cancel_flags;
        u32 open_flags;
        u32 statx_flags;
        u32 fadvise_advice;
        u32 splice_flags;
        u32 rename_flags;
        u32 unlink_flags;
        u32 hardlink_flags;
        u32 xattr_flags;
        u32 msg_ring_flags;
        u32 uring_cmd_flags;
    };
    u64     user_data;      /* Passed untranslated to completion event (CQE) */
    union {
        struct {
            union {
                u16 buf_index;      /* Index into registered fixed buffers */
                u16 buf_group;      /* Buffer group for provided buffers */
            };
            u16 personality;        /* Credentials/personality ID */
            i32 splice_fd_in;       /* Source fd for splice/tee */
        };
        u64 __pad2[3];
    };
} __attribute__((packed)) io_uring_sqe_t;

/* ─── Completion Queue Entry (CQE) ────────────────────────────────────────── */
typedef struct io_uring_cqe {
    u64     user_data;      /* sqe->user_data value */
    i32     res;            /* Result: >= 0 bytes transferred, or -errno */
    u32     flags;          /* IORING_CQE_F_* flags */
} __attribute__((packed)) io_uring_cqe_t;

/* ─── SQE Flags ───────────────────────────────────────────────────────────── */
#define IOSQE_FIXED_FILE_BIT                0
#define IOSQE_IO_DRAIN_BIT                  1
#define IOSQE_IO_LINK_BIT                   2
#define IOSQE_IO_HARDLINK_BIT               3
#define IOSQE_ASYNC_BIT                     4
#define IOSQE_BUFFER_SELECT_BIT             5
#define IOSQE_CQE_SKIP_SUCCESS_BIT          6

#define IOSQE_FIXED_FILE                    (1u << IOSQE_FIXED_FILE_BIT)
#define IOSQE_IO_DRAIN                      (1u << IOSQE_IO_DRAIN_BIT)
#define IOSQE_IO_LINK                       (1u << IOSQE_IO_LINK_BIT)
#define IOSQE_IO_HARDLINK                   (1u << IOSQE_IO_HARDLINK_BIT)
#define IOSQE_ASYNC                         (1u << IOSQE_ASYNC_BIT)
#define IOSQE_BUFFER_SELECT                 (1u << IOSQE_BUFFER_SELECT_BIT)
#define IOSQE_CQE_SKIP_SUCCESS              (1u << IOSQE_CQE_SKIP_SUCCESS_BIT)

/* ─── CQE Flags ───────────────────────────────────────────────────────────── */
#define IORING_CQE_F_BUFFER                 (1u << 0)
#define IORING_CQE_F_MORE                   (1u << 1)
#define IORING_CQE_F_SOCK_NONEMPTY          (1u << 2)
#define IORING_CQE_F_NOTIF                  (1u << 3)

#define IORING_CQE_BUFFER_SHIFT             16

/* ─── io_uring Operation Codes (Opcodes) ──────────────────────────────────── */
typedef enum {
    IORING_OP_NOP                           = 0,
    IORING_OP_READV                         = 1,
    IORING_OP_WRITEV                        = 2,
    IORING_OP_FSYNC                         = 3,
    IORING_OP_READ_FIXED                    = 4,
    IORING_OP_WRITE_FIXED                   = 5,
    IORING_OP_POLL_ADD                      = 6,
    IORING_OP_POLL_REMOVE                   = 7,
    IORING_OP_SYNC_FILE_RANGE               = 8,
    IORING_OP_SENDMSG                       = 9,
    IORING_OP_RECVMSG                       = 10,
    IORING_OP_TIMEOUT                       = 11,
    IORING_OP_TIMEOUT_REMOVE                = 12,
    IORING_OP_ACCEPT                        = 13,
    IORING_OP_ASYNC_CANCEL                  = 14,
    IORING_OP_LINK_TIMEOUT                  = 15,
    IORING_OP_CONNECT                       = 16,
    IORING_OP_FALLOCATE                     = 17,
    IORING_OP_OPENAT                        = 18,
    IORING_OP_CLOSE                         = 19,
    IORING_OP_FILES_UPDATE                  = 20,
    IORING_OP_STATX                         = 21,
    IORING_OP_READ                          = 22,
    IORING_OP_WRITE                         = 23,
    IORING_OP_FADVISE                       = 24,
    IORING_OP_MADVISE                       = 25,
    IORING_OP_SEND                          = 26,
    IORING_OP_RECV                          = 27,
    IORING_OP_OPENAT2                       = 28,
    IORING_OP_EPOLL_CTL                     = 29,
    IORING_OP_SPLICE                        = 30,
    IORING_OP_PROVIDE_BUFFERS               = 31,
    IORING_OP_REMOVE_BUFFERS                = 32,
    IORING_OP_TEE                           = 33,
    IORING_OP_SHUTDOWN                      = 34,
    IORING_OP_RENAMEAT                      = 35,
    IORING_OP_UNLINKAT                      = 36,
    IORING_OP_MKDIRAT                       = 37,
    IORING_OP_SYMLINKAT                     = 38,
    IORING_OP_LINKAT                        = 39,
    IORING_OP_MSG_RING                      = 40,
    IORING_OP_FSETXATTR                     = 41,
    IORING_OP_SETXATTR                      = 42,
    IORING_OP_FGETXATTR                     = 43,
    IORING_OP_GETXATTR                      = 44,
    IORING_OP_SOCKET                        = 45,
    IORING_OP_URING_CMD                     = 46,
    IORING_OP_SEND_ZC                       = 47,
    IORING_OP_SENDMSG_ZC                    = 48,
    IORING_OP_LAST                          = 49
} io_uring_op_t;

/* ─── io_uring Setup Flags ────────────────────────────────────────────────── */
#define IORING_SETUP_IOPOLL                 (1u << 0)
#define IORING_SETUP_SQPOLL                 (1u << 1)
#define IORING_SETUP_SQ_AFF                 (1u << 2)
#define IORING_SETUP_CQSIZE                 (1u << 3)
#define IORING_SETUP_CLAMP                  (1u << 4)
#define IORING_SETUP_ATTACH_WQ              (1u << 5)
#define IORING_SETUP_R_DISABLED             (1u << 6)
#define IORING_SETUP_SUBMIT_ALL             (1u << 7)
#define IORING_SETUP_COOP_TASKRUN           (1u << 8)
#define IORING_SETUP_TASKRUN_FLAG           (1u << 9)
#define IORING_SETUP_SQE128                 (1u << 10)
#define IORING_SETUP_CQE32                  (1u << 11)
#define IORING_SETUP_SINGLE_ISSUER          (1u << 12)
#define IORING_SETUP_DEFER_TASKRUN          (1u << 13)

/* ─── io_uring Kernel Features ────────────────────────────────────────────── */
#define IORING_FEAT_SINGLE_MMAP             (1u << 0)
#define IORING_FEAT_NODROP                  (1u << 1)
#define IORING_FEAT_SUBMIT_STABLE           (1u << 2)
#define IORING_FEAT_RW_CUR_POS              (1u << 3)
#define IORING_FEAT_CUR_PERSONALITY         (1u << 4)
#define IORING_FEAT_FAST_POLL               (1u << 5)
#define IORING_FEAT_POLL_32BITS             (1u << 6)
#define IORING_FEAT_SQPOLL_NONFIXED         (1u << 7)
#define IORING_FEAT_EXT_ARG                 (1u << 8)
#define IORING_FEAT_NATIVE_WORKERS          (1u << 9)
#define IORING_FEAT_RSRC_TAGS               (1u << 10)
#define IORING_FEAT_CQE_SKIP                (1u << 11)
#define IORING_FEAT_LINKED_FILE             (1u << 12)

/* ─── Ring Offsets for mmap() ─────────────────────────────────────────────── */
typedef struct io_sqring_offsets {
    u32     head;
    u32     tail;
    u32     ring_mask;
    u32     ring_entries;
    u32     flags;
    u32     dropped;
    u32     array;
    u32     resv1;
    u64     user_addr;
} io_sqring_offsets_t;

typedef struct io_cqring_offsets {
    u32     head;
    u32     tail;
    u32     ring_mask;
    u32     ring_entries;
    u32     overflow;
    u32     cqes;
    u32     flags;
    u32     resv1;
    u64     user_addr;
} io_cqring_offsets_t;

typedef struct io_uring_params {
    u32                 sq_entries;
    u32                 cq_entries;
    u32                 flags;
    u32                 sq_thread_cpu;
    u32                 sq_thread_idle;
    u32                 features;
    u32                 wq_fd;
    u32                 resv[3];
    io_sqring_offsets_t sq_off;
    io_cqring_offsets_t cq_off;
} io_uring_params_t;

/* ─── Register Operation Codes ────────────────────────────────────────────── */
#define IORING_REGISTER_BUFFERS             0
#define IORING_UNREGISTER_BUFFERS           1
#define IORING_REGISTER_FILES               2
#define IORING_UNREGISTER_FILES             3
#define IORING_REGISTER_EVENTFD             4
#define IORING_UNREGISTER_EVENTFD           5
#define IORING_REGISTER_FILES_UPDATE        6
#define IORING_REGISTER_EVENTFD_ASYNC       7
#define IORING_REGISTER_PROBE               8
#define IORING_REGISTER_PERSONALITY         9
#define IORING_UNREGISTER_PERSONALITY       10
#define IORING_REGISTER_ENABLE_RINGS        11
#define IORING_REGISTER_RESTRICTIONS        12
#define IORING_REGISTER_CALLBACK            13
#define IORING_REGISTER_IOWQ_AFF            14
#define IORING_UNREGISTER_IOWQ_AFF          15
#define IORING_REGISTER_IOWQ_MAX_WORKERS    16

/* ─── Registered Buffer Structure ─────────────────────────────────────────── */
typedef struct io_uring_reg_buf {
    u64     addr;
    u64     len;
    u64     phys_addr;
    u8      is_pinned;
} io_uring_reg_buf_t;

/* ─── In-Kernel Ring Context ──────────────────────────────────────────────── */
#define IO_URING_MAX_ENTRIES                4096
#define IO_URING_MAX_FIXED_BUFFERS          256
#define IO_URING_MAX_FIXED_FILES            1024

typedef struct io_ring_ctx {
    u32                 id;
    u32                 flags;
    u32                 sq_entries;
    u32                 cq_entries;
    u32                 sq_mask;
    u32                 cq_mask;

    /* Shared Memory Pointers */
    volatile u32*       sq_head;
    volatile u32*       sq_tail;
    volatile u32*       sq_flags;
    volatile u32*       sq_dropped;
    volatile u32*       sq_array;
    io_uring_sqe_t*     sqes;

    volatile u32*       cq_head;
    volatile u32*       cq_tail;
    volatile u32*       cq_flags;
    volatile u32*       cq_overflow;
    io_uring_cqe_t*     cqes;

    /* Registered Resources */
    io_uring_reg_buf_t  fixed_buffers[IO_URING_MAX_FIXED_BUFFERS];
    u32                 nr_fixed_buffers;
    i32                 fixed_files[IO_URING_MAX_FIXED_FILES];
    u32                 nr_fixed_files;

    /* SQPOLL Thread Management */
    u32                 sq_thread_cpu;
    u32                 sq_thread_idle_ms;
    u32                 sq_thread_last_active;
    u8                  sq_thread_running;
    u8                  sq_thread_should_stop;

    /* Telemetry */
    u64                 stat_sqes_submitted;
    u64                 stat_cqes_completed;
    u64                 stat_iops_counter;
    u64                 stat_zero_copy_hits;
    u64                 stat_sqpoll_cycles;

    u8                  is_active;
} io_ring_ctx_t;

/* ─── Public Kernel API ───────────────────────────────────────────────────── */
int  io_uring_subsystem_init(void);
i32  sys_io_uring_setup(u32 entries, io_uring_params_t* params);
i32  sys_io_uring_enter(i32 fd, u32 to_submit, u32 min_complete, u32 flags, const void* sig);
i32  sys_io_uring_register(i32 fd, u32 opcode, const void* arg, u32 nr_args);

io_ring_ctx_t* io_uring_get_context(i32 fd);
int  io_uring_submit_sqes(io_ring_ctx_t* ctx, u32 count);
int  io_uring_complete_cqe(io_ring_ctx_t* ctx, u64 user_data, i32 res, u32 flags);
void io_uring_dump_telemetry(i32 fd);
int  io_uring_run_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* IO_URING_H */
