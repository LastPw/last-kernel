#ifndef IO_URING_H
#define IO_URING_H

#include "types.h"

#define IORING_OP_NOP        0
#define IORING_OP_READV      1
#define IORING_OP_WRITEV     2
#define IORING_OP_FSYNC      3
#define IORING_OP_POLL_ADD   4
#define IORING_OP_POLL_REMOVE 5

typedef struct {
    u8 opcode;
    u8 flags;
    u16 ioprio;
    i32 fd;
    u64 off;
    u64 addr;
    u32 len;
    u32 op_flags;
    u64 user_data;
} io_uring_sqe_t;

typedef struct {
    u64 user_data;
    i32 res;
    u32 flags;
} io_uring_cqe_t;

typedef struct {
    u32 sq_head;
    u32 sq_tail;
    u32 cq_head;
    u32 cq_tail;
    u32 ring_entries;
    io_uring_sqe_t sqes[32];
    io_uring_cqe_t cqes[32];
} io_uring_instance_t;

int io_uring_init(void);
int io_uring_create(u32 entries, io_uring_instance_t* out_ring);
int io_uring_submit(io_uring_instance_t* ring);

#endif
