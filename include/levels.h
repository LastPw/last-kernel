#ifndef LEVELS_H
#define LEVELS_H

#include "types.h"

#define LEVEL_COUNT 7u

/* Global gates used to evaluate project maturity by level. */
#define LEVEL_GATE_BOOT     0x0001u
#define LEVEL_GATE_MM       0x0002u
#define LEVEL_GATE_SCHED    0x0004u
#define LEVEL_GATE_VFS      0x0008u
#define LEVEL_GATE_NET      0x0010u
#define LEVEL_GATE_SEC      0x0020u
#define LEVEL_GATE_PERF     0x0040u
#define LEVEL_GATE_VM_COW   0x0080u

typedef struct {
    u8 level_id;
    u8 passed;
    u16 reserved;
    u32 required_gates;
    u32 met_gates;
    u32 readiness_percent;
} level_info_t;

typedef struct {
    u8 target_level;
    u8 current_level;
    u16 reserved;
    u32 met_gates;
    u32 score_percent;
    u32 blockers;
    u32 tick;
} level_status_t;

void levels_init(void);
void levels_tick(void);
int levels_set_target(u8 level_id);
void levels_status(level_status_t* out);
int levels_get_info(u8 level_id, level_info_t* out);

#endif
