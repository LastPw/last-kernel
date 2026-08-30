#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include "io_uring.h"

/* Host Mocks */
void serial_write(const char* s) { (void)s; }
void serial_write_dec(u32 v) { (void)v; }
void serial_write_hex(u32 v) { (void)v; }
void spinlock_init(void* lock) { (void)lock; }
void spinlock_lock(void* lock) { (void)lock; }
void spinlock_unlock(void* lock) { (void)lock; }

static void test_io_uring_lifecycle(void) {
    printf("  [1/4] Testing io_uring Ring Allocation & Parameter Offsets... ");
    io_uring_params_t p;
    memset(&p, 0, sizeof(p));
    p.flags = IORING_SETUP_CQSIZE;
    p.cq_entries = 1024;

    i32 fd = sys_io_uring_setup(512, &p);
    assert(fd > 0);
    assert(p.sq_entries == 512);
    assert(p.cq_entries == 1024);
    assert(p.sq_off.user_addr != 0);
    assert(p.cq_off.user_addr != 0);
    assert(p.features & IORING_FEAT_SINGLE_MMAP);
    printf("PASS (SQ: 512, CQ: 1024, Ring FD: %d)\n", fd);
}

static void test_io_uring_registered_buffers(void) {
    printf("  [2/4] Testing Pre-Registered Zero-Copy Memory Buffers... ");
    io_uring_params_t p;
    memset(&p, 0, sizeof(p));
    i32 fd = sys_io_uring_setup(128, &p);
    assert(fd > 0);

    struct { u64 iov_base; u64 iov_len; } bufs[4] = {
        { 0x100000, 4096 },
        { 0x200000, 8192 },
        { 0x300000, 16384 },
        { 0x400000, 65536 }
    };

    i32 reg_res = sys_io_uring_register(fd, IORING_REGISTER_BUFFERS, bufs, 4);
    assert(reg_res == 0);

    io_ring_ctx_t* ctx = io_uring_get_context(fd);
    assert(ctx != NULL);
    assert(ctx->nr_fixed_buffers == 4);
    assert(ctx->fixed_buffers[3].len == 65536);
    printf("PASS (4 Fixed Buffers, 64KB Max Pinned)\n");
}

static void test_io_uring_batch_submission_throughput(void) {
    printf("  [3/4] Testing High-Throughput Batch SQE Submission & CQE Validation... ");
    io_uring_params_t p;
    memset(&p, 0, sizeof(p));
    p.flags = IORING_SETUP_CQSIZE;
    p.cq_entries = 2048;

    i32 fd = sys_io_uring_setup(1024, &p);
    assert(fd > 0);

    io_ring_ctx_t* ctx = io_uring_get_context(fd);
    assert(ctx != NULL);

    /* Submit 500 operations in a single batch */
    static u8 s_test_user_buffer[4096];
    const u32 BATCH_COUNT = 500;
    u32 tail = *ctx->sq_tail;

    for (u32 i = 0; i < BATCH_COUNT; i++) {
        u32 idx = (tail + i) & ctx->sq_mask;
        memset(&ctx->sqes[idx], 0, sizeof(io_uring_sqe_t));
        ctx->sqes[idx].opcode = (i % 2 == 0) ? IORING_OP_NOP : IORING_OP_READ;
        ctx->sqes[idx].fd = 10 + (i % 8);
        ctx->sqes[idx].addr = (u64)(uintptr_t)s_test_user_buffer;
        ctx->sqes[idx].len = 512;
        ctx->sqes[idx].user_data = 0xCAFE0000 | i;
    }
    *ctx->sq_tail = tail + BATCH_COUNT;

    i32 submitted = sys_io_uring_enter(fd, BATCH_COUNT, BATCH_COUNT, 0, NULL);
    assert(submitted == (i32)BATCH_COUNT);

    /* Validate all 500 CQE completions */
    u32 cq_head = *ctx->cq_head;
    u32 cq_tail = *ctx->cq_tail;
    assert((cq_tail - cq_head) == BATCH_COUNT);

    for (u32 i = 0; i < BATCH_COUNT; i++) {
        u32 c_idx = (cq_head + i) & ctx->cq_mask;
        assert(ctx->cqes[c_idx].user_data == (0xCAFE0000 | i));
    }
    *ctx->cq_head = cq_head + BATCH_COUNT;
    printf("PASS (Processed %u SQEs & CQEs Seamlessly)\n", BATCH_COUNT);
}

static void test_io_uring_kernel_selftest(void) {
    printf("  [4/4] Testing In-Kernel io_uring Subsystem Diagnostic Suite... ");
    int res = io_uring_run_selftest();
    assert(res == 0);
    printf("PASS (Zero-Copy Read/Write & Splice Complete)\n");
}

int main(void) {
    printf("\n=======================================================\n");
    printf("   LastKernel io_uring Asynchronous I/O Test Suite     \n");
    printf("=======================================================\n");
    test_io_uring_lifecycle();
    test_io_uring_registered_buffers();
    test_io_uring_batch_submission_throughput();
    test_io_uring_kernel_selftest();
    printf(">>> ALL IO_URING ENGINE TESTS PASSED 100%% SUCCESSFULLY! <<<\n\n");
    return 0;
}
