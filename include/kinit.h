#ifndef KINIT_H
#define KINIT_H

#include "types.h"

typedef enum {
    KINIT_LVL_EARLY = 0,
    KINIT_LVL_ARCH,
    KINIT_LVL_MM,
    KINIT_LVL_SUBSYS,
    KINIT_LVL_ENGINE,
    KINIT_LVL_SCHED,
    KINIT_LVL_SERVICE,
    KINIT_LVL_RUNTIME,
    KINIT_LVL_LATE,
    KINIT_LVL_MAX
} kinit_level_t;

typedef int (*kinit_fn_t)(void);

typedef struct {
    const char* name;
    kinit_level_t level;
    kinit_fn_t init;
} kinit_module_t;

typedef struct {
    u32 total;
    u32 done;
    u32 failed;
    u32 last_rc;
    u32 last_index;
    kinit_level_t last_level;
} kinit_status_t;

void kinit_reset(void);
int kinit_register(const kinit_module_t* mods, u32 count);
int kinit_run_level(kinit_level_t level);
int kinit_run_all(void);
void kinit_status(kinit_status_t* out);
int kinit_module_info(u32 index, kinit_module_t* out, i32* out_rc);

#endif
