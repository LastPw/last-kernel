#ifndef SELFTEST_H
#define SELFTEST_H

#include "types.h"

typedef struct {
    u32 total;
    u32 passed;
    u32 failed;
    u32 last_step;
    u32 last_error;
    u32 last_tick;
} selftest_result_t;

void selftest_init(void);
int selftest_run_full(selftest_result_t* out);
int selftest_run_stress(u32 loops, selftest_result_t* out);
int selftest_run_fuzz(u32 rounds, selftest_result_t* out);
int selftest_run_linux_abi(selftest_result_t* out);
void selftest_status(selftest_result_t* out);

#endif
