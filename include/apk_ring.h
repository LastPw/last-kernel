#ifndef APK_RING_H
#define APK_RING_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APK_RING_QUEUE_DEPTH 128

#define APK_OP_NOP        0
#define APK_OP_READ       1
#define APK_OP_WRITE      2
#define APK_OP_NET_SEND   3
#define APK_OP_NET_RECV   4
#define APK_OP_IPC_NOTIFY 5

/* Submission Queue Entry (SQE) */
typedef struct {
    u8  opcode;
    u8  flags;
    u16 ioprio;
    u32 fd_or_dev;
    u64 addr;
    u32 len;
    u64 offset;
    u64 user_data;
} __attribute__((aligned(64))) apk_sqe_t;

/* Completion Queue Entry (CQE) */
typedef struct {
    u64 user_data;
    i32 res;
    u32 flags;
} __attribute__((aligned(16))) apk_cqe_t;

typedef struct {
    volatile u32 sq_head;
    volatile u32 sq_tail;
    volatile u32 cq_head;
    volatile u32 cq_tail;
    u32 queue_depth;
    apk_sqe_t sqes[APK_RING_QUEUE_DEPTH];
    apk_cqe_t cqes[APK_RING_QUEUE_DEPTH];
} apk_ring_t;

int  apk_ring_init(apk_ring_t* ring);
int  apk_ring_submit_sqe(apk_ring_t* ring, const apk_sqe_t* sqe);
int  apk_ring_reap_cqe(apk_ring_t* ring, apk_cqe_t* out_cqe);
u32  apk_ring_dispatch_all(apk_ring_t* ring);

#ifdef __cplusplus
}
#endif

#endif /* APK_RING_H */
